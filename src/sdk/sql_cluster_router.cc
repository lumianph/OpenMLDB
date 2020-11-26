/*
 * sql_cluster_router.cc
 * Copyright (C) 4paradigm.com 2020 wangtaize <wangtaize@4paradigm.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "sdk/sql_cluster_router.h"

#include <memory>
#include <string>
#include <utility>

#include "brpc/channel.h"
#include "glog/logging.h"
#include "parser/parser.h"
#include "plan/planner.h"
#include "proto/tablet.pb.h"
#include "sdk/base.h"
#include "sdk/base_impl.h"
#include "sdk/result_set_sql.h"
#include "sdk/batch_request_result_set_sql.h"
#include "timer.h"  //NOLINT
#include "boost/none.hpp"
#include "rpc/rpc_client.h"

namespace rtidb {
namespace sdk {

class ExplainInfoImpl : public ExplainInfo {
 public:
    ExplainInfoImpl(const ::fesql::sdk::SchemaImpl& input_schema,
                    const ::fesql::sdk::SchemaImpl& output_schema,
                    const std::string& logical_plan,
                    const std::string& physical_plan, const std::string& ir,
                    const std::string& request_name)
        : input_schema_(input_schema),
          output_schema_(output_schema),
          logical_plan_(logical_plan),
          physical_plan_(physical_plan),
          ir_(ir),
          request_name_(request_name) {}
    ~ExplainInfoImpl() {}

    const ::fesql::sdk::Schema& GetInputSchema() { return input_schema_; }

    const ::fesql::sdk::Schema& GetOutputSchema() { return output_schema_; }

    const std::string& GetLogicalPlan() { return logical_plan_; }

    const std::string& GetPhysicalPlan() { return physical_plan_; }

    const std::string& GetIR() { return ir_; }

    const std::string& GetRequestName() { return request_name_; }

 private:
    ::fesql::sdk::SchemaImpl input_schema_;
    ::fesql::sdk::SchemaImpl output_schema_;
    std::string logical_plan_;
    std::string physical_plan_;
    std::string ir_;
    std::string request_name_;
};

class ProcedureInfoImpl : public ProcedureInfo {
 public:
     ProcedureInfoImpl(const std::string& db_name, const std::string& sp_name,
             const std::string& sql,
             const ::fesql::sdk::SchemaImpl& input_schema,
             const ::fesql::sdk::SchemaImpl& output_schema,
             const std::vector<std::string>& tables,
             const std::string& main_table)
        : db_name_(db_name),
          sp_name_(sp_name),
          sql_(sql),
          input_schema_(input_schema),
          output_schema_(output_schema),
          tables_(tables),
          main_table_(main_table) {}

    ~ProcedureInfoImpl() {}

    const ::fesql::sdk::Schema& GetInputSchema() { return input_schema_; }

    const ::fesql::sdk::Schema& GetOutputSchema() { return output_schema_; }

    const std::string& GetDbName() { return db_name_; }

    const std::string& GetSpName() { return sp_name_; }

    const std::string& GetSql() { return sql_; }

    const std::vector<std::string>& GetTables() { return tables_; }

    const std::string& GetMainTable() { return main_table_; }

 private:
    std::string db_name_;
    std::string sp_name_;
    std::string sql_;
    ::fesql::sdk::SchemaImpl input_schema_;
    ::fesql::sdk::SchemaImpl output_schema_;
    std::vector<std::string> tables_;
    std::string main_table_;
};

class QueryFutureImpl : public QueryFuture {
 public:
    explicit QueryFutureImpl(rtidb::RpcCallback<rtidb::api::QueryResponse>* callback)
        : callback_(callback) {
            if (callback_) {
                callback_->Ref();
            }
    }
    ~QueryFutureImpl() {
        if (callback_) {
            callback_->UnRef();
        }
    }

    std::shared_ptr<fesql::sdk::ResultSet> GetResultSet(fesql::sdk::Status* status) override {
        if (!callback_ || !status || !callback_->GetResponse() || !callback_->GetController()) {
            status->code = fesql::common::kRpcError;
            status->msg = "request error, response or controller null";
            return nullptr;
        }
        brpc::Join(callback_->GetController()->call_id());
        if (callback_->GetController()->Failed()) {
            status->code = fesql::common::kRpcError;
            status->msg = "request error, " + callback_->GetController()->ErrorText();
            return nullptr;
        }
        std::shared_ptr<::rtidb::sdk::ResultSetSQL> rs = std::make_shared<rtidb::sdk::ResultSetSQL>(
                    callback_->GetResponse(), callback_->GetController());
        bool ok = rs->Init();
        if (!ok) {
            status->code = -1;
            status->msg = "request error, resuletSetSQL init failed";
            return nullptr;
        }
        return rs;
    }

    bool IsDone() const override {
        return callback_->IsDone();
    }

 private:
    rtidb::RpcCallback<rtidb::api::QueryResponse>* callback_;
};

class BatchQueryFutureImpl : public QueryFuture {
 public:
    explicit BatchQueryFutureImpl(
            rtidb::RpcCallback<rtidb::api::SQLBatchRequestQueryResponse>* callback)
        : callback_(callback) {
            if (callback_) {
                callback_->Ref();
            }
    }

    ~BatchQueryFutureImpl() {
        if (callback_) {
            callback_->UnRef();
        }
    }

    std::shared_ptr<fesql::sdk::ResultSet> GetResultSet(fesql::sdk::Status* status) override {
        if (!callback_ || !status || !callback_->GetResponse() || !callback_->GetController()) {
            status->code = fesql::common::kRpcError;
            status->msg = "request error, response or controller null";
            return nullptr;
        }
        brpc::Join(callback_->GetController()->call_id());
        if (callback_->GetController()->Failed()) {
            status->code = fesql::common::kRpcError;
            status->msg = "request error. " + callback_->GetController()->ErrorText();
            return nullptr;
        }
        std::shared_ptr<::rtidb::sdk::SQLBatchRequestResultSet> rs =
            std::make_shared<rtidb::sdk::SQLBatchRequestResultSet>(
                    callback_->GetResponse(), callback_->GetController());
        bool ok = rs->Init();
        if (!ok) {
            status->code = -1;
            status->msg = "request error, resuletSetSQL init failed";
            return nullptr;
        }
        return rs;
    }

    bool IsDone() const override {
        return callback_->IsDone();
    }

 private:
    rtidb::RpcCallback<rtidb::api::SQLBatchRequestQueryResponse>* callback_;
};

SQLClusterRouter::SQLClusterRouter(const SQLRouterOptions& options)
    : options_(options),
      cluster_sdk_(NULL),
      engine_(NULL),
      input_lru_cache_(),
      mu_(),
      rand_(::baidu::common::timer::now_time()) {}

SQLClusterRouter::SQLClusterRouter(ClusterSDK* sdk)
    : options_(),
      cluster_sdk_(sdk),
      engine_(NULL),
      input_lru_cache_(),
      mu_(),
      rand_(::baidu::common::timer::now_time()) {}

SQLClusterRouter::~SQLClusterRouter() {
    delete cluster_sdk_;
    delete engine_;
}

bool SQLClusterRouter::Init() {
    if (cluster_sdk_ == NULL) {
        ClusterOptions coptions;
        coptions.zk_cluster = options_.zk_cluster;
        coptions.zk_path = options_.zk_path;
        coptions.session_timeout = options_.session_timeout;
        cluster_sdk_ = new ClusterSDK(coptions);
        bool ok = cluster_sdk_->Init();
        if (!ok) {
            LOG(WARNING) << "fail to init cluster sdk";
            return false;
        }
    }
    ::fesql::vm::Engine::InitializeGlobalLLVM();
    ::fesql::vm::EngineOptions eopt;
    eopt.set_compile_only(true);
    eopt.set_plan_only(true);
    engine_ = new ::fesql::vm::Engine(cluster_sdk_->GetCatalog(), eopt);
    return true;
}

std::shared_ptr<SQLRequestRow> SQLClusterRouter::GetRequestRow(
    const std::string& db, const std::string& sql,
    ::fesql::sdk::Status* status) {
    if (status == NULL) return std::shared_ptr<SQLRequestRow>();
    std::shared_ptr<RouterCache> cache = GetCache(db, sql);
    if (cache) {
        status->code = 0;
        return std::make_shared<SQLRequestRow>(cache->column_schema);
    }
    ::fesql::vm::ExplainOutput explain;
    ::fesql::base::Status vm_status;
    bool ok = engine_->Explain(sql, db, ::fesql::vm::kRequestMode, &explain, &vm_status);
    if (!ok) {
        status->code = -1;
        status->msg = vm_status.msg;
        LOG(WARNING) << "fail to explain sql " << sql << " for "
                     << vm_status.msg;
        return std::shared_ptr<SQLRequestRow>();
    }
    std::shared_ptr<::fesql::sdk::SchemaImpl> schema =
        std::make_shared<::fesql::sdk::SchemaImpl>(explain.input_schema);
    SetCache(db, sql, std::make_shared<RouterCache>(schema));
    return std::make_shared<SQLRequestRow>(schema);
}

std::shared_ptr<SQLInsertRow> SQLClusterRouter::GetInsertRow(
    const std::string& db, const std::string& sql,
    ::fesql::sdk::Status* status) {
    if (status == NULL) return std::shared_ptr<SQLInsertRow>();
    std::shared_ptr<RouterCache> cache = GetCache(db, sql);
    if (cache) {
        status->code = 0;
        return std::make_shared<SQLInsertRow>(
            cache->table_info, cache->column_schema, cache->default_map,
            cache->str_length);
    }
    std::shared_ptr<::rtidb::nameserver::TableInfo> table_info;
    DefaultValueMap default_map;
    uint32_t str_length = 0;
    if (!GetInsertInfo(db, sql, status, &table_info, &default_map,
                       &str_length)) {
        status->code = 1;
        LOG(WARNING) << "get insert information failed";
        return std::shared_ptr<SQLInsertRow>();
    }
    cache = std::make_shared<RouterCache>(table_info, default_map, str_length);
    SetCache(db, sql, cache);
    return std::make_shared<SQLInsertRow>(table_info, cache->column_schema,
                                          default_map, str_length);
}

bool SQLClusterRouter::GetInsertInfo(
    const std::string& db, const std::string& sql, ::fesql::sdk::Status* status,
    std::shared_ptr<::rtidb::nameserver::TableInfo>* table_info,
    DefaultValueMap* default_map, uint32_t* str_length) {
    if (status == NULL || table_info == NULL || default_map == NULL ||
        str_length == NULL) {
        LOG(WARNING) << "insert info is null" << sql;
        return false;
    }
    ::fesql::node::NodeManager nm;
    ::fesql::plan::PlanNodeList plans;
    bool ok = GetSQLPlan(sql, &nm, &plans);
    if (!ok || plans.size() == 0) {
        LOG(WARNING) << "fail to get sql plan with sql " << sql;
        status->msg = "fail to get sql plan with";
        return false;
    }
    ::fesql::node::PlanNode* plan = plans[0];
    if (plan->GetType() != fesql::node::kPlanTypeInsert) {
        status->msg = "invalid sql node expect insert";
        LOG(WARNING) << "invalid sql node expect insert";
        return false;
    }
    ::fesql::node::InsertPlanNode* iplan =
        dynamic_cast<::fesql::node::InsertPlanNode*>(plan);
    const ::fesql::node::InsertStmt* insert_stmt = iplan->GetInsertNode();
    if (insert_stmt == NULL) {
        LOG(WARNING) << "insert stmt is null";
        status->msg = "insert stmt is null";
        return false;
    }
    *table_info = cluster_sdk_->GetTableInfo(db, insert_stmt->table_name_);
    if (!(*table_info)) {
        status->msg =
            "table with name " + insert_stmt->table_name_ + " does not exist";
        LOG(WARNING) << status->msg;
        return false;
    }
    std::map<uint32_t, uint32_t> column_map;
    for (size_t j = 0; j < insert_stmt->columns_.size(); ++j) {
        const std::string& col_name = insert_stmt->columns_[j];
        bool find_flag = false;
        for (int i = 0; i < (*table_info)->column_desc_v1_size(); ++i) {
            if (col_name == (*table_info)->column_desc_v1(i).name()) {
                if (column_map.count(i) > 0) {
                    status->msg = "duplicate column of " + col_name;
                    LOG(WARNING) << status->msg;
                    return false;
                }
                column_map.insert(std::make_pair(i, j));
                find_flag = true;
                break;
            }
        }
        if (!find_flag) {
            status->msg = "can't find column " + col_name + " in table " +
                          (*table_info)->name();
            LOG(WARNING) << status->msg;
            return false;
        }
    }
    *default_map = GetDefaultMap(
        *table_info, column_map,
        dynamic_cast<::fesql::node::ExprListNode*>(insert_stmt->values_[0]),
        str_length);
    if (!(*default_map)) {
        status->msg = "get default value map of " + sql + " failed";
        LOG(WARNING) << status->msg;
        return false;
    }
    return true;
}

std::shared_ptr<fesql::node::ConstNode> SQLClusterRouter::GetDefaultMapValue(
    const fesql::node::ConstNode& node, rtidb::type::DataType column_type) {
    fesql::node::DataType node_type = node.GetDataType();
    switch (column_type) {
        case rtidb::type::kBool:
            if (node_type == fesql::node::kInt32) {
                return std::make_shared<fesql::node::ConstNode>(node);
            }
            break;
        case rtidb::type::kSmallInt:
            if (node_type == fesql::node::kInt16) {
                return std::make_shared<fesql::node::ConstNode>(node);
            } else if (node_type == fesql::node::kInt32) {
                return std::make_shared<fesql::node::ConstNode>(
                    node.GetAsInt16());
            }
            break;
        case rtidb::type::kInt:
            if (node_type == fesql::node::kInt16) {
                return std::make_shared<fesql::node::ConstNode>(
                    node.GetAsInt32());
            } else if (node_type == fesql::node::kInt32) {
                return std::make_shared<fesql::node::ConstNode>(node);
            }
            break;
        case rtidb::type::kBigInt:
            if (node_type == fesql::node::kInt16 ||
                node_type == fesql::node::kInt32) {
                return std::make_shared<fesql::node::ConstNode>(
                    node.GetAsInt64());
            } else if (node_type == fesql::node::kInt64) {
                return std::make_shared<fesql::node::ConstNode>(node);
            }
            break;
        case rtidb::type::kFloat:
            if (node_type == fesql::node::kDouble ||
                node_type == fesql::node::kInt32 ||
                node_type == fesql::node::kInt16) {
                return std::make_shared<fesql::node::ConstNode>(
                    node.GetAsFloat());
            } else if (node_type == fesql::node::kFloat) {
                return std::make_shared<fesql::node::ConstNode>(node);
            }
            break;
        case rtidb::type::kDouble:
            if (node_type == fesql::node::kFloat ||
                node_type == fesql::node::kInt32 ||
                node_type == fesql::node::kInt16) {
                return std::make_shared<fesql::node::ConstNode>(
                    node.GetAsDouble());
            } else if (node_type == fesql::node::kDouble) {
                return std::make_shared<fesql::node::ConstNode>(node);
            }
            break;
        case rtidb::type::kDate:
            if (node_type == fesql::node::kVarchar) {
                int32_t year;
                int32_t month;
                int32_t day;
                if (node.GetAsDate(&year, &month, &day)) {
                    if (year < 1900 || year > 9999) break;
                    if (month < 1 || month > 12) break;
                    if (day < 1 || day > 31) break;
                    int32_t date = (year - 1900) << 16;
                    date = date | ((month - 1) << 8);
                    date = date | day;
                    return std::make_shared<fesql::node::ConstNode>(date);
                }
                break;
            } else if (node_type == fesql::node::kDate) {
                return std::make_shared<fesql::node::ConstNode>(node);
            }
            break;
        case rtidb::type::kTimestamp:
            if (node_type == fesql::node::kInt16 ||
                node_type == fesql::node::kInt32 ||
                node_type == fesql::node::kTimestamp) {
                return std::make_shared<fesql::node::ConstNode>(
                    node.GetAsInt64());
            } else if (node_type == fesql::node::kInt64) {
                return std::make_shared<fesql::node::ConstNode>(node);
            }
            break;
        case rtidb::type::kVarchar:
        case rtidb::type::kString:
            if (node_type == fesql::node::kVarchar) {
                return std::make_shared<fesql::node::ConstNode>(node);
            }
            break;
        default:
            return std::shared_ptr<fesql::node::ConstNode>();
    }
    return std::shared_ptr<fesql::node::ConstNode>();
}

DefaultValueMap SQLClusterRouter::GetDefaultMap(
    std::shared_ptr<::rtidb::nameserver::TableInfo> table_info,
    const std::map<uint32_t, uint32_t>& column_map,
    ::fesql::node::ExprListNode* row, uint32_t* str_length) {
    if (row == NULL || str_length == NULL) {
        LOG(WARNING) << "row or str length is NULL";
        return DefaultValueMap();
    }
    DefaultValueMap default_map(
        new std::map<uint32_t, std::shared_ptr<::fesql::node::ConstNode>>());
    if ((column_map.empty() && static_cast<int32_t>(row->children_.size()) <
                                   table_info->column_desc_v1_size()) ||
        (!column_map.empty() && row->children_.size() < column_map.size())) {
        LOG(WARNING) << "insert value number less than column number";
        return DefaultValueMap();
    }
    for (int32_t idx = 0; idx < table_info->column_desc_v1_size(); idx++) {
        if (!column_map.empty() && (column_map.count(idx) == 0)) {
            if (table_info->column_desc_v1(idx).not_null()) {
                LOG(WARNING)
                    << "column " << table_info->column_desc_v1(idx).name()
                    << " can't be null";
                return DefaultValueMap();
            }
            default_map->insert(std::make_pair(
                idx, std::make_shared<::fesql::node::ConstNode>()));
            continue;
        }

        auto column = table_info->column_desc_v1(idx);
        uint32_t i = idx;
        if (!column_map.empty()) {
            i = column_map.at(idx);
        }
        ::fesql::node::ConstNode* primary =
            dynamic_cast<::fesql::node::ConstNode*>(row->children_.at(i));
        if (!primary->IsPlaceholder()) {
            std::shared_ptr<::fesql::node::ConstNode> val;
            if (primary->IsNull()) {
                if (column.not_null()) {
                    LOG(WARNING)
                        << "column " << column.name() << " can't be null";
                    return DefaultValueMap();
                }
                val = std::make_shared<::fesql::node::ConstNode>(*primary);
            } else {
                val = GetDefaultMapValue(*primary, column.data_type());
                if (!val) {
                    LOG(WARNING) << "default value type mismatch, column "
                                 << column.name();
                    return DefaultValueMap();
                }
            }
            default_map->insert(std::make_pair(idx, val));
            if (!primary->IsNull() &&
                (column.data_type() == ::rtidb::type::kVarchar ||
                 column.data_type() == ::rtidb::type::kString)) {
                *str_length += strlen(primary->GetStr());
            }
        }
    }
    return default_map;
}

std::shared_ptr<RouterCache> SQLClusterRouter::GetCache(
    const std::string& db, const std::string& sql) {
    std::lock_guard<::rtidb::base::SpinMutex> lock(mu_);
    auto it = input_lru_cache_.find(db);
    if (it != input_lru_cache_.end()) {
        auto value = it->second.get(sql);
        if (value != boost::none) {
            return value.value();
        }
    }
    return std::shared_ptr<RouterCache>();
}

void SQLClusterRouter::SetCache(const std::string& db, const std::string& sql,
                                std::shared_ptr<RouterCache> router_cache) {
    std::lock_guard<::rtidb::base::SpinMutex> lock(mu_);
    auto it = input_lru_cache_.find(db);
    if (it == input_lru_cache_.end()) {
        boost::compute::detail::lru_cache<std::string, std::shared_ptr<::rtidb::sdk::RouterCache>>
            sql_cache(options_.max_sql_cache_size);
        input_lru_cache_.insert(std::make_pair(db, sql_cache));
        it = input_lru_cache_.find(db);
    }
    it->second.insert(sql, router_cache);
}

std::shared_ptr<SQLInsertRows> SQLClusterRouter::GetInsertRows(
    const std::string& db, const std::string& sql,
    ::fesql::sdk::Status* status) {
    if (status == NULL) return std::shared_ptr<SQLInsertRows>();
    std::shared_ptr<RouterCache> cache = GetCache(db, sql);
    if (cache) {
        status->code = 0;
        return std::make_shared<SQLInsertRows>(
            cache->table_info, cache->column_schema, cache->default_map,
            cache->str_length);
    }
    std::shared_ptr<::rtidb::nameserver::TableInfo> table_info;
    DefaultValueMap default_map;
    uint32_t str_length = 0;
    if (!GetInsertInfo(db, sql, status, &table_info, &default_map,
                       &str_length)) {
        return std::shared_ptr<SQLInsertRows>();
    }
    cache = std::make_shared<RouterCache>(table_info, default_map, str_length);
    SetCache(db, sql, cache);
    return std::make_shared<SQLInsertRows>(table_info, cache->column_schema,
                                           default_map, str_length);
}

bool SQLClusterRouter::ExecuteDDL(const std::string& db, const std::string& sql,
                                  fesql::sdk::Status* status) {
    auto ns_ptr = cluster_sdk_->GetNsClient();
    if (!ns_ptr) {
        status->code = -1;
        status->msg = "no nameserver exist";
        LOG(WARNING) << status->msg;
        return false;
    }
    // TODO(wangtaize) update ns client to thread safe
    std::string err;
    bool ok = false;

    // parse sql to judge whether is create procedure case
    fesql::node::NodeManager node_manager;
    fesql::parser::FeSQLParser parser;
    DLOG(INFO) << "start to execute script from dbms:\n" << sql;
    fesql::base::Status sql_status;
    fesql::node::NodePointVector parser_trees;
    parser.parse(sql, parser_trees, &node_manager, sql_status);
    if (parser_trees.empty() || sql_status.code != 0) {
        status->code = -1;
        status->msg = sql_status.msg;
        LOG(WARNING) << status->msg;
        return false;
    }
    fesql::node::SQLNode* node = parser_trees[0];
    if (node->GetType() == fesql::node::kCreateSpStmt) {
        ok = HandleSQLCreateProcedure(parser_trees, db, sql,
                ns_ptr, &node_manager, &err);
    } else {
        ok = ns_ptr->ExecuteSQL(db, sql, err);
    }
    if (!ok) {
        status->msg = "fail to execute sql " + sql + " for error " + err;
        LOG(WARNING) << status->msg;
        status->code = -1;
        return false;
    }
    return true;
}

bool SQLClusterRouter::ShowDB(std::vector<std::string>* dbs,
                              fesql::sdk::Status* status) {
    auto ns_ptr = cluster_sdk_->GetNsClient();
    if (!ns_ptr) {
        LOG(WARNING) << "no nameserver exist";
        return false;
    }
    std::string err;
    bool ok = ns_ptr->ShowDatabase(dbs, err);
    if (!ok) {
        status->msg = "fail to show databases: " + err;
        LOG(WARNING) << status->msg;
        status->code = -1;
        return false;
    }
    return true;
}
bool SQLClusterRouter::CreateDB(const std::string& db,
                                fesql::sdk::Status* status) {
    if (status == NULL) {
        return false;
    }

    auto ns_ptr = cluster_sdk_->GetNsClient();
    if (!ns_ptr) {
        LOG(WARNING) << "no nameserver exist";
        status->msg = "no nameserver exist";
        status->code = -1;
        return false;
    }
    if (db.empty()) {
        LOG(WARNING) << "db is empty";
        status->msg = "db is emtpy";
        status->code = -2;
        return false;
    }
    std::string err;
    bool ok = ns_ptr->CreateDatabase(db, err);
    if (!ok) {
        LOG(WARNING) << "fail to create db " << db << " for error " << err;
        status->msg = err;
        return false;
    }
    status->code = 0;
    return true;
}

bool SQLClusterRouter::DropDB(const std::string& db,
                              fesql::sdk::Status* status) {
    auto ns_ptr = cluster_sdk_->GetNsClient();
    if (!ns_ptr) {
        LOG(WARNING) << "no nameserver exist";
        return false;
    }
    std::string err;
    bool ok = ns_ptr->DropDatabase(db, err);
    if (!ok) {
        LOG(WARNING) << "fail to drop db " << db << " for error " << err;
        return false;
    }
    return true;
}

std::shared_ptr<::rtidb::client::TabletClient> SQLClusterRouter::GetTabletClient(
    const std::string& db, const std::string& sql) {
    // TODO(wangtaize) cache compile result
    std::set<std::string> tables;
    ::fesql::base::Status status;
    if (!engine_->GetDependentTables(sql, db, ::fesql::vm::kBatchMode, &tables, status)) {
        LOG(WARNING) << "fail to get tablet: " << status.msg;
        return std::shared_ptr<::rtidb::client::TabletClient>();
    }

    // pick one tablet for const query sql
    if (tables.empty()) {
        auto tablet = cluster_sdk_->GetTablet();
        if (!tablet) {
            LOG(WARNING) << "fail to pick a tablet";
            return std::shared_ptr<::rtidb::client::TabletClient>();
        }
        return tablet->GetClient();
    }
    const std::string& name = *tables.begin();
    auto tablet = cluster_sdk_->GetTablet(db, name);
    if (!tablet) {
        LOG(WARNING) << "fail to get table " << name << " tablet";
        return std::shared_ptr<::rtidb::client::TabletClient>();
    }
    return tablet->GetClient();
}

std::shared_ptr<rtidb::client::TabletClient> SQLClusterRouter::GetTablet(
        const std::string& db, const std::string& sp_name, fesql::sdk::Status* status) {
    if (status == nullptr) return nullptr;
    ::rtidb::api::ProcedureInfo sp_info;
    if (!cluster_sdk_->GetProcedureInfo(db, sp_name, &sp_info, &status->msg)) {
        status->code = -1;
        status->msg = "procedure not found, msg: " + status->msg;
        LOG(WARNING) << status->msg;
        return nullptr;
    }
    const std::string& table = sp_info.main_table();
    auto tablet = cluster_sdk_->GetTablet(db, table);
    if (!tablet) {
        status->code = -1;
        status->msg = "fail to get tablet, table "  + table;
        LOG(WARNING) << status->msg;
        return nullptr;
    }
    return tablet->GetClient();
}

bool SQLClusterRouter::IsConstQuery(::fesql::vm::PhysicalOpNode* node) {
    if (node->GetOpType() == ::fesql::vm::kPhysicalOpConstProject) {
        return true;
    }

    if (node->GetProducerCnt() <= 0) {
        return false;
    }

    for (size_t i = 0; i < node->GetProducerCnt(); i++) {
        if (!IsConstQuery(node->GetProducer(i))) {
            return false;
        }
    }
    return true;
}
void SQLClusterRouter::GetTables(::fesql::vm::PhysicalOpNode* node,
                                 std::set<std::string>* tables) {
    if (node == NULL || tables == NULL) return;
    if (node->GetOpType() == ::fesql::vm::kPhysicalOpDataProvider) {
        ::fesql::vm::PhysicalDataProviderNode* data_node =
            reinterpret_cast<::fesql::vm::PhysicalDataProviderNode*>(node);
        if (data_node->provider_type_ == ::fesql::vm::kProviderTypeTable ||
            data_node->provider_type_ == ::fesql::vm::kProviderTypePartition) {
            tables->insert(data_node->table_handler_->GetName());
        }
    }
    if (node->GetProducerCnt() <= 0) return;
    for (size_t i = 0; i < node->GetProducerCnt(); i++) {
        GetTables(node->GetProducer(i), tables);
    }
}

std::shared_ptr<fesql::sdk::ResultSet> SQLClusterRouter::ExecuteSQL(
    const std::string& db, const std::string& sql,
    std::shared_ptr<SQLRequestRow> row, fesql::sdk::Status* status) {
    if (!row || !status) {
        LOG(WARNING) << "input is invalid";
        return std::shared_ptr<::fesql::sdk::ResultSet>();
    }
    if (!row->OK()) {
        LOG(WARNING) << "make sure the request row is built before execute sql";
        return std::shared_ptr<::fesql::sdk::ResultSet>();
    }
    std::shared_ptr<::brpc::Controller> cntl(new ::brpc::Controller());
    std::shared_ptr<::rtidb::api::QueryResponse> response(
        new ::rtidb::api::QueryResponse());
    auto client = GetTabletClient(db, sql);
    if (!client) {
        status->msg = "not tablet found";
        return std::shared_ptr<::fesql::sdk::ResultSet>();
    }
    if (!client->Query(db, sql, row->GetRow(), cntl.get(), response.get(),
                             options_.enable_debug)) {
        status->msg = "request server error";
        return std::shared_ptr<::fesql::sdk::ResultSet>();
    }
    if (response->code() != ::rtidb::base::kOk) {
        return std::shared_ptr<::fesql::sdk::ResultSet>();
    }
    std::shared_ptr<::rtidb::sdk::ResultSetSQL> rs(
        new rtidb::sdk::ResultSetSQL(response, cntl));
    if (!rs->Init()) {
        return std::shared_ptr<::fesql::sdk::ResultSet>();
    }
    return rs;
}

std::shared_ptr<::fesql::sdk::ResultSet> SQLClusterRouter::ExecuteSQL(
    const std::string& db, const std::string& sql,
    ::fesql::sdk::Status* status) {
    std::shared_ptr<::brpc::Controller> cntl(new ::brpc::Controller());
    std::shared_ptr<::rtidb::api::QueryResponse> response(
        new ::rtidb::api::QueryResponse());
    auto client = GetTabletClient(db, sql);
    if (!client) {
        DLOG(INFO) << "no tablet avilable for sql " << sql;
        return std::shared_ptr<::fesql::sdk::ResultSet>();
    }
    DLOG(INFO) << " send query to tablet " << client->GetEndpoint();
    if (!client->Query(db, sql, cntl.get(), response.get(),
                           options_.enable_debug)) {
        return std::shared_ptr<::fesql::sdk::ResultSet>();
    }
    std::shared_ptr<::rtidb::sdk::ResultSetSQL> rs(
        new rtidb::sdk::ResultSetSQL(response, cntl));
    if (!rs->Init()) {
        DLOG(INFO) << "fail to init result set for sql " << sql;
        return std::shared_ptr<::fesql::sdk::ResultSet>();
    }
    return rs;
}

std::shared_ptr<fesql::sdk::ResultSet> SQLClusterRouter::ExecuteSQLBatchRequest(
    const std::string& db, const std::string& sql,
    std::shared_ptr<SQLRequestRowBatch> row_batch, fesql::sdk::Status* status) {
    if (!row_batch || !status) {
        LOG(WARNING) << "input is invalid";
        return nullptr;
    }
    std::shared_ptr<::brpc::Controller> cntl(new ::brpc::Controller());
    std::shared_ptr<::rtidb::api::SQLBatchRequestQueryResponse> response(
        new ::rtidb::api::SQLBatchRequestQueryResponse());
    auto client = GetTabletClient(db, sql);
    if (!client) {
        status->code = -1;
        status->msg = "no tablet found";
        return nullptr;
    }
    if (!client->SQLBatchRequestQuery(db, sql, row_batch, cntl.get(),
                                            response.get(),
                                            options_.enable_debug)) {
        status->code = -1;
        status->msg = "request server error " + response->msg();
        return nullptr;
    }
    if (response->code() != ::rtidb::base::kOk) {
        status->code = -1;
        status->msg = response->msg();
        return nullptr;
    }
    std::shared_ptr<::rtidb::sdk::SQLBatchRequestResultSet> rs(
        new rtidb::sdk::SQLBatchRequestResultSet(response, cntl));
    if (!rs->Init()) {
        status->code = -1;
        status->msg = "batch request result set init fail";
        return nullptr;
    }
    return rs;
}

bool SQLClusterRouter::ExecuteInsert(const std::string& db,
                                     const std::string& sql,
                                     ::fesql::sdk::Status* status) {
    if (status == NULL) return false;
    std::shared_ptr<::rtidb::nameserver::TableInfo> table_info;
    DefaultValueMap default_map;
    uint32_t str_length = 0;
    if (!GetInsertInfo(db, sql, status, &table_info, &default_map,
                       &str_length)) {
        status->code = 1;
        LOG(WARNING) << "get insert information failed";
        return false;
    }
    std::shared_ptr<SQLInsertRow> row = std::make_shared<SQLInsertRow>(
        table_info, ::rtidb::sdk::ConvertToSchema(table_info), default_map,
        str_length);
    if (!row) {
        status->msg = "fail to parse row from sql " + sql;
        LOG(WARNING) << status->msg;
        return false;
    }
    if (!row->Init(0)) {
        status->msg = "fail to encode row for table " + table_info->name();
        LOG(WARNING) << status->msg;
        return false;
    }
    if (!row->IsComplete()) {
        status->msg = "insert value isn't complete";
        LOG(WARNING) << status->msg;
        return false;
    }
    std::vector<std::shared_ptr<::rtidb::catalog::TabletAccessor>> tablets;
    bool ret = cluster_sdk_->GetTablet(db, table_info->name(), &tablets);
    if (!ret || tablets.empty()) {
        status->msg = "fail to get table " + table_info->name() + " tablet";
        LOG(WARNING) << status->msg;
        return false;
    }
    return PutRow(table_info->tid(), row, tablets, status);
}

bool SQLClusterRouter::PutRow(uint32_t tid, const std::shared_ptr<SQLInsertRow>& row,
        const std::vector<std::shared_ptr<::rtidb::catalog::TabletAccessor>>& tablets,
        ::fesql::sdk::Status* status) {
    if (status == nullptr) {
        return false;
    }
    auto dimensions = row->GetDimensions();
    for (const auto& kv : dimensions) {
        uint32_t pid = kv.first;
        if (pid < tablets.size()) {
            auto tablet = tablets[pid];
            if (tablet) {
                auto client = tablet->GetClient();
                if (client) {
                    DLOG(INFO) << "put data to endpoint " << client->GetEndpoint()
                               << " with dimensions size " << kv.second.size();
                    if (!client->Put(tid, pid, kv.second, row->GetTs(), row->GetRow(), 1)) {
                        status->msg = "fail to make a put request to table. tid " + std::to_string(tid);
                        LOG(WARNING) << status->msg;
                        return false;
                    }
                    continue;
                }
            }
        }
        status->msg = "fail to get tablet client. pid " + std::to_string(pid);
        LOG(WARNING) << status->msg;
        return false;
    }
    return true;
}

bool SQLClusterRouter::ExecuteInsert(const std::string& db,
                                     const std::string& sql,
                                     std::shared_ptr<SQLInsertRows> rows,
                                     fesql::sdk::Status* status) {
    if (!rows || !status) {
        LOG(WARNING) << "input is invalid";
        return false;
    }
    std::shared_ptr<RouterCache> cache = GetCache(db, sql);
    if (cache) {
        std::shared_ptr<::rtidb::nameserver::TableInfo> table_info =
            cache->table_info;
        std::vector<std::shared_ptr<::rtidb::catalog::TabletAccessor>> tablets;
        bool ret = cluster_sdk_->GetTablet(db, table_info->name(), &tablets);
        if (!ret || tablets.empty()) {
            status->msg = "fail to get table " + table_info->name() + " tablet";
            LOG(WARNING) << status->msg;
            return false;
        }
        for (uint32_t i = 0; i < rows->GetCnt(); ++i) {
            std::shared_ptr<SQLInsertRow> row = rows->GetRow(i);
            if (!PutRow(table_info->tid(), row, tablets, status)) {
                return false;
            }
        }
        return true;
    } else {
        status->msg = "please use getInsertRow with " + sql + " first";
        LOG(WARNING) << status->msg;
        return false;
    }
}

bool SQLClusterRouter::ExecuteInsert(const std::string& db,
                                     const std::string& sql,
                                     std::shared_ptr<SQLInsertRow> row,
                                     fesql::sdk::Status* status) {
    if (!row || !status) {
        status->msg = "input is invalid";
        LOG(WARNING) << "input is invalid";
        return false;
    }
    std::shared_ptr<RouterCache> cache = GetCache(db, sql);
    if (cache) {
        std::shared_ptr<::rtidb::nameserver::TableInfo> table_info = cache->table_info;
        std::vector<std::shared_ptr<::rtidb::catalog::TabletAccessor>> tablets;
        bool ret = cluster_sdk_->GetTablet(db, table_info->name(), &tablets);
        if (!ret || tablets.empty()) {
            status->msg = "fail to get table " + table_info->name() + " tablet";
            LOG(WARNING) << status->msg;
            return false;
        }
        if (!PutRow(table_info->tid(), row, tablets, status)) {
            return false;
        }
        return true;
    } else {
        status->msg = "please use getInsertRow with " + sql + " first";
        LOG(WARNING) << status->msg;
        return false;
    }
}

bool SQLClusterRouter::GetSQLPlan(const std::string& sql,
                                  ::fesql::node::NodeManager* nm,
                                  ::fesql::node::PlanNodeList* plan) {
    if (nm == NULL || plan == NULL) return false;
    ::fesql::parser::FeSQLParser parser;
    ::fesql::plan::SimplePlanner planner(nm);
    ::fesql::base::Status sql_status;
    ::fesql::node::NodePointVector parser_trees;
    parser.parse(sql, parser_trees, nm, sql_status);
    if (0 != sql_status.code) {
        LOG(WARNING) << sql_status.msg;
        return false;
    }
    planner.CreatePlanTree(parser_trees, *plan, sql_status);
    if (0 != sql_status.code) {
        LOG(WARNING) << sql_status.msg;
        return false;
    }
    return true;
}

bool SQLClusterRouter::RefreshCatalog() {
    bool ok = cluster_sdk_->Refresh();
    if (ok) {
        engine_->UpdateCatalog(cluster_sdk_->GetCatalog());
    }
    return ok;
}

std::shared_ptr<ExplainInfo> SQLClusterRouter::Explain(
    const std::string& db, const std::string& sql,
    ::fesql::sdk::Status* status) {
    ::fesql::vm::ExplainOutput explain_output;
    ::fesql::base::Status vm_status;
    bool ok = engine_->Explain(sql, db, ::fesql::vm::kRequestMode, &explain_output, &vm_status);
    if (!ok) {
        status->code = -1;
        status->msg = vm_status.msg;
        LOG(WARNING) << "fail to explain sql " << sql;
        return std::shared_ptr<ExplainInfo>();
    }
    ::fesql::sdk::SchemaImpl input_schema(explain_output.input_schema);
    ::fesql::sdk::SchemaImpl output_schema(explain_output.output_schema);
    std::shared_ptr<ExplainInfoImpl> impl(new ExplainInfoImpl(
        input_schema, output_schema, explain_output.logical_plan,
        explain_output.physical_plan, explain_output.ir, explain_output.request_name));
    return impl;
}

std::shared_ptr<fesql::sdk::ResultSet> SQLClusterRouter::CallProcedure(
    const std::string& db, const std::string& sp_name,
    std::shared_ptr<SQLRequestRow> row, fesql::sdk::Status* status) {
    if (!row || !status) {
        status->code = -1;
        status->msg = "input is invalid";
        LOG(WARNING) << status->msg;
        return nullptr;
    }
    if (!row->OK()) {
        status->code = -1;
        status->msg = "make sure the request row is built before execute sql";
        LOG(WARNING) << "make sure the request row is built before execute sql";
        return nullptr;
    }
    auto tablet = GetTablet(db, sp_name, status);
    if (!tablet) {
        return nullptr;
    }

    std::shared_ptr<::brpc::Controller> cntl(new ::brpc::Controller());
    std::shared_ptr<::rtidb::api::QueryResponse> response(
        new ::rtidb::api::QueryResponse());
    bool ok = tablet->CallProcedure(db, sp_name, row->GetRow(), cntl.get(), response.get(),
                             options_.enable_debug);
    if (!ok) {
        status->code = -1;
        status->msg = "request server error";
        LOG(WARNING) << status->msg;
        return nullptr;
    }
    if (response->code() != ::rtidb::base::kOk) {
        status->code = -1;
        status->msg = response->msg();
        LOG(WARNING) << status->msg;
        return nullptr;
    }
    std::shared_ptr<::rtidb::sdk::ResultSetSQL> rs(
        new rtidb::sdk::ResultSetSQL(response, cntl));
    if (!rs->Init()) {
        status->code = -1;
        status->msg = "resuletSetSQL init failed";
        LOG(WARNING) << status->msg;
        return nullptr;
    }
    return rs;
}

std::shared_ptr<fesql::sdk::ResultSet> SQLClusterRouter::CallSQLBatchRequestProcedure(
        const std::string& db, const std::string& sp_name,
        std::shared_ptr<SQLRequestRowBatch> row_batch, fesql::sdk::Status* status) {
    if (!row_batch || !status) {
        status->code = -1;
        status->msg = "input is invalid";
        return nullptr;
    }
    auto tablet = GetTablet(db, sp_name, status);
    if (!tablet) {
        return nullptr;
    }

    std::shared_ptr<::brpc::Controller> cntl(new ::brpc::Controller());
    std::shared_ptr<::rtidb::api::SQLBatchRequestQueryResponse> response(
        new ::rtidb::api::SQLBatchRequestQueryResponse());
    bool ok = tablet->CallSQLBatchRequestProcedure(
            db, sp_name, row_batch, cntl.get(), response.get(),
            options_.enable_debug);
    if (!ok) {
        status->code = -1;
        status->msg = "request server error";
        return nullptr;
    }
    if (response->code() != ::rtidb::base::kOk) {
        status->code = -1;
        status->msg = response->msg();
        return nullptr;
    }
    auto rs = std::make_shared<::rtidb::sdk::SQLBatchRequestResultSet>(response, cntl);
    if (!rs->Init()) {
        status->code = -1;
        status->msg = "resuletSetSQL init failed";
        return nullptr;
    }
    return rs;
}

std::shared_ptr<ProcedureInfo> SQLClusterRouter::ShowProcedure(
        const std::string& db, const std::string& sp_name, fesql::sdk::Status* status) {
    if (status == nullptr) {
        status->code = -1;
        status->msg = "null ptr";
        LOG(WARNING) << status->msg;
        return std::shared_ptr<ProcedureInfo>();
    }
    ::rtidb::api::ProcedureInfo sp_info;
    if (!cluster_sdk_->GetProcedureInfo(db, sp_name, &sp_info, &status->msg)) {
        status->code = -1;
        status->msg = "procedure not found, msg: " + status->msg;
        LOG(WARNING) << status->msg;
        return std::shared_ptr<ProcedureInfo>();
    }
    ::fesql::vm::Schema fesql_in_schema;
    if (!rtidb::catalog::SchemaAdapter::ConvertSchema(
                sp_info.input_schema(), &fesql_in_schema)) {
        status->msg = "fail to convert input schema";
        LOG(WARNING) << status->msg;
        status->code = -1;
        return std::shared_ptr<ProcedureInfo>();
    }
    ::fesql::vm::Schema fesql_out_schema;
    if (!rtidb::catalog::SchemaAdapter::ConvertSchema(
                sp_info.output_schema(), &fesql_out_schema)) {
        status->msg = "fail to convert output schema";
        LOG(WARNING) << status->msg;
        status->code = -1;
        return std::shared_ptr<ProcedureInfo>();
    }
    ::fesql::sdk::SchemaImpl input_schema(fesql_in_schema);
    ::fesql::sdk::SchemaImpl output_schema(fesql_out_schema);
    std::vector<std::string> table_vec;
    const auto& tables = sp_info.tables();
    for (const auto& table : tables) {
        table_vec.push_back(table);
    }
    std::shared_ptr<ProcedureInfoImpl> sp_info_impl = std::make_shared<ProcedureInfoImpl>(
            db, sp_name, sp_info.sql(), input_schema, output_schema, table_vec, sp_info.main_table());
    return sp_info_impl;
}

bool SQLClusterRouter::HandleSQLCreateProcedure(const fesql::node::NodePointVector& parser_trees,
        const std::string& db, const std::string& sql,
        std::shared_ptr<::rtidb::client::NsClient> ns_ptr,
        fesql::node::NodeManager* node_manager, std::string* msg) {
    if (node_manager == nullptr || msg == nullptr) {
        return false;
    }
    fesql::plan::SimplePlanner planner(node_manager);
    fesql::node::PlanNodeList plan_trees;
    fesql::base::Status sql_status;
    planner.CreatePlanTree(parser_trees, plan_trees, sql_status);
    if (plan_trees.empty() || sql_status.code != 0) {
        *msg = sql_status.msg;
        return false;
    }

    fesql::node::PlanNode* plan = plan_trees[0];
    if (plan == nullptr) {
        *msg = "fail to execute plan : PlanNode null";
        return false;
    }
    switch (plan->GetType()) {
        case fesql::node::kPlanTypeCreateSp: {
            fesql::node::CreateProcedurePlanNode* create_sp =
                dynamic_cast<fesql::node::CreateProcedurePlanNode*>(plan);
            if (create_sp == nullptr) {
                *msg = "cast CreateProcedurePlanNode failed";
                return false;
            }
            // construct sp_info
            rtidb::api::ProcedureInfo sp_info;
            sp_info.set_db_name(db);
            sp_info.set_sp_name(create_sp->GetSpName());
            sp_info.set_sql(sql);
            RtidbSchema* schema = sp_info.mutable_input_schema();
            for (auto input : create_sp->GetInputParameterList()) {
                if (input == nullptr) {
                    *msg = "fail to execute plan : InputParameterNode null";
                    return false;
                }
                if (input->GetType() == fesql::node::kInputParameter) {
                    fesql::node::InputParameterNode* input_ptr =
                        (fesql::node::InputParameterNode*)input;
                    if (input_ptr == nullptr) {
                        *msg = "cast InputParameterNode failed";
                        return false;
                    }
                    rtidb::common::ColumnDesc* col_desc = schema->Add();
                    col_desc->set_name(input_ptr->GetColumnName());
                    rtidb::type::DataType rtidb_type;
                    bool ok = ::rtidb::catalog::SchemaAdapter::ConvertType(input_ptr->GetColumnType(),
                            &rtidb_type);
                    if (!ok) {
                        *msg = "convert type failed";
                        return false;
                    }
                    col_desc->set_data_type(rtidb_type);
                    col_desc->set_is_constant(input_ptr->GetIsConstant());
                } else {
                    *msg = "fail to execute script with unSuppurt type" +
                        fesql::node::NameOfSQLNodeType(input->GetType());
                    return false;
                }
            }
            // get input schema, check input parameter, and fill sp_info
            fesql::vm::ExplainOutput explain_output;
            bool ok = engine_->Explain(sql, db, fesql::vm::kRequestMode, &explain_output, &sql_status);
            if (!ok) {
                *msg = "fail to explain sql";
                return false;
            }
            RtidbSchema rtidb_input_schema;
            if (!rtidb::catalog::SchemaAdapter::ConvertSchema(explain_output.input_schema, &rtidb_input_schema)) {
                *msg = "convert input schema failed";
                return false;
            }
            if (!CheckParameter(*schema, rtidb_input_schema)) {
                *msg = "check input parameter failed";
                return false;
            }
            sp_info.mutable_input_schema()->CopyFrom(*schema);
            // get output schema, and fill sp_info
            RtidbSchema rtidb_output_schema;
            if (!rtidb::catalog::SchemaAdapter::ConvertSchema(explain_output.output_schema, &rtidb_output_schema)) {
                *msg = "convert output schema failed";
                return false;
            }
            sp_info.mutable_output_schema()->CopyFrom(rtidb_output_schema);
            // TODO(wb) get main table
            sp_info.set_main_table(explain_output.request_name);
            // get dependent tables, and fill sp_info
            std::set<std::string> tables;
            ::fesql::base::Status status;
            if (!engine_->GetDependentTables(sql, db, ::fesql::vm::kRequestMode, &tables, status)) {
                LOG(WARNING) << "fail to get dependent tables: " << status.msg;
                return false;
            }
            for (auto& table : tables) {
                sp_info.add_tables(table);
            }
            // send request to ns client
            if (!ns_ptr->CreateProcedure(sp_info, msg)) {
                *msg = "create procedure failed, msg: " + *msg;
                return false;
            }
            break;
        }
        default: {
            *msg = "fail to execute script with unSuppurt type " +
                              fesql::node::NameOfPlanNodeType(plan->GetType());
            return false;
        }
    }
    return true;
}

bool SQLClusterRouter::CheckParameter(const RtidbSchema& parameter,
        const RtidbSchema& input_schema) {
    if (parameter.size() != input_schema.size()) {
        return false;
    }
    for (int32_t i = 0; i < parameter.size(); i++) {
        if (parameter.Get(i).name() != input_schema.Get(i).name()) {
            LOG(WARNING) << "check column name failed, expect " << input_schema.Get(i).name()
                << ", but " << parameter.Get(i).name();
            return false;
        }
        if (parameter.Get(i).data_type() != input_schema.Get(i).data_type()) {
            LOG(WARNING) << "check column type failed, expect "
                << rtidb::type::DataType_Name(input_schema.Get(i).data_type())
                << ", but " << rtidb::type::DataType_Name(parameter.Get(i).data_type());
            return false;
        }
    }
    return true;
}

bool SQLClusterRouter::ShowProcedure(
        std::vector<std::shared_ptr<::rtidb::api::ProcedureInfo>>* sp_infos, std::string* msg) {
    if (msg == nullptr || sp_infos == nullptr) {
        *msg = "null ptr";
        return false;
    }
    if (!cluster_sdk_->GetProcedureInfo(sp_infos, msg)) {
        return false;
    }
    return true;
}

bool SQLClusterRouter::ShowProcedure(const std::string& db, const std::string& sp_name,
        ::rtidb::api::ProcedureInfo* sp_info, std::string* msg) {
    if (msg == nullptr || sp_info == nullptr) {
        *msg = "null ptr";
        return false;
    }
    if (!cluster_sdk_->GetProcedureInfo(db, sp_name, sp_info, msg)) {
        return false;
    }
    return true;
}

std::shared_ptr<rtidb::sdk::QueryFuture> SQLClusterRouter::CallProcedure(
    const std::string& db, const std::string& sp_name, int64_t timeout_ms,
    std::shared_ptr<SQLRequestRow> row, fesql::sdk::Status* status) {
    if (!row || !status) {
        status->code = -1;
        status->msg = "input is invalid";
        LOG(WARNING) << status->msg;
        return std::shared_ptr<rtidb::sdk::QueryFuture>();
    }
    if (!row->OK()) {
        status->code = -1;
        status->msg = "make sure the request row is built before execute sql";
        LOG(WARNING) << "make sure the request row is built before execute sql";
        return std::shared_ptr<rtidb::sdk::QueryFuture>();
    }
    auto tablet = GetTablet(db, sp_name, status);
    if (!tablet) {
        return std::shared_ptr<rtidb::sdk::QueryFuture>();
    }

    std::shared_ptr<rtidb::api::QueryResponse> response =
        std::make_shared<rtidb::api::QueryResponse>();
    std::shared_ptr<brpc::Controller> cntl = std::make_shared<brpc::Controller>();
    rtidb::RpcCallback<rtidb::api::QueryResponse>* callback =
            new rtidb::RpcCallback<rtidb::api::QueryResponse>(response, cntl);

    std::shared_ptr<rtidb::sdk::QueryFutureImpl> future =
        std::make_shared<rtidb::sdk::QueryFutureImpl>(callback);
    bool ok = tablet->CallProcedure(db, sp_name, row->GetRow(), timeout_ms,
            options_.enable_debug, callback);
    if (!ok) {
        status->code = -1;
        status->msg = "request server error";
        LOG(WARNING) << status->msg;
        return std::shared_ptr<rtidb::sdk::QueryFuture>();
    }
    return future;
}

std::shared_ptr<rtidb::sdk::QueryFuture> SQLClusterRouter::CallSQLBatchRequestProcedure(
        const std::string& db, const std::string& sp_name, int64_t timeout_ms,
        std::shared_ptr<SQLRequestRowBatch> row_batch, fesql::sdk::Status* status) {
    if (!row_batch || !status) {
        status->code = -1;
        status->msg = "input is invalid";
        return nullptr;
    }
    auto tablet = GetTablet(db, sp_name, status);
    if (!tablet) {
        return nullptr;
    }

    std::shared_ptr<brpc::Controller> cntl = std::make_shared<brpc::Controller>();
    std::shared_ptr<rtidb::api::SQLBatchRequestQueryResponse> response =
        std::make_shared<rtidb::api::SQLBatchRequestQueryResponse>();
    rtidb::RpcCallback<rtidb::api::SQLBatchRequestQueryResponse>* callback =
           new rtidb::RpcCallback<rtidb::api::SQLBatchRequestQueryResponse>(response, cntl);

    std::shared_ptr<rtidb::sdk::BatchQueryFutureImpl> future =
        std::make_shared<rtidb::sdk::BatchQueryFutureImpl>(callback);
    bool ok = tablet->CallSQLBatchRequestProcedure(
            db, sp_name, row_batch, options_.enable_debug, timeout_ms, callback);
    if (!ok) {
        status->code = -1;
        status->msg = "request server error";
        LOG(WARNING) << status->msg;
        return nullptr;
    }
    return future;
}

}  // namespace sdk
}  // namespace rtidb