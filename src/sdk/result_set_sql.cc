/*
 * result_set_impl.cc
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

#include "sdk/result_set_sql.h"

#include <memory>
#include <string>
#include <utility>

#include "base/fe_strings.h"
#include "base/status.h"
#include "codec/fe_schema_codec.h"
#include "glog/logging.h"

namespace rtidb {
namespace sdk {

ResultSetSQL::ResultSetSQL(
    const std::shared_ptr<::rtidb::api::QueryResponse>& response,
    const std::shared_ptr<brpc::Controller>& cntl)
    : response_(response),
      index_(-1),
      byte_size_(0),
      position_(0),
      row_view_(),
      internal_schema_(),
      schema_(),
      cntl_(cntl) {}

ResultSetSQL::~ResultSetSQL() {}

bool ResultSetSQL::Init() {
    if (!response_ || response_->code() != ::rtidb::base::kOk) {
        LOG(WARNING) << "bad response code " << response_->code();
        return false;
    }
    byte_size_ = response_->byte_size();
    DLOG(INFO) << "byte size " << byte_size_ << " count " << response_->count();
    if (byte_size_ <= 0) return true;
    bool ok = ::fesql::codec::SchemaCodec::Decode(response_->schema(),
                                                  &internal_schema_);
    if (!ok) {
        LOG(WARNING) << "fail to decode response schema ";
        return false;
    }
    std::unique_ptr<::fesql::sdk::RowIOBufView> row_view(
        new ::fesql::sdk::RowIOBufView(internal_schema_));
    row_view_ = std::move(row_view);
    schema_.SetSchema(internal_schema_);
    return true;
}

bool ResultSetSQL::IsNULL(int index) { return row_view_->IsNULL(index); }

bool ResultSetSQL::Next() {
    index_++;
    if (index_ < static_cast<int32_t>(response_->count()) &&
        position_ < byte_size_) {
        // get row size
        uint32_t row_size = 0;
        cntl_->response_attachment().copy_to(reinterpret_cast<void*>(&row_size),
                                             4, position_ + 2);
        DLOG(INFO) << "row size " << row_size << " position " << position_
                   << " byte size " << byte_size_;
        butil::IOBuf tmp;
        cntl_->response_attachment().append_to(&tmp, row_size, position_);
        position_ += row_size;
        bool ok = row_view_->Reset(tmp);
        if (!ok) {
            LOG(WARNING) << "reset row buf failed";
            return false;
        }
        return true;
    }
    return false;
}

bool ResultSetSQL::Reset() {
    index_ = -1;
    position_ = 0;
    return true;
}

bool ResultSetSQL::GetString(uint32_t index, std::string* str) {
    if (str == NULL) {
        LOG(WARNING) << "input ptr is null pointer";
        return false;
    }
    butil::IOBuf tmp;
    int32_t ret = row_view_->GetString(index, &tmp);
    if (ret == 0) {
        DLOG(INFO) << "get str size " << tmp.size();
        tmp.append_to(str, tmp.size(), 0);
        return true;
    }
    DLOG(INFO) << "fail to get string with ret " << ret;
    return false;
}

bool ResultSetSQL::GetBool(uint32_t index, bool* val) {
    if (val == NULL) {
        LOG(WARNING) << "input ptr is null pointer";
        return false;
    }
    int32_t ret = row_view_->GetBool(index, val);
    return ret == 0;
}

bool ResultSetSQL::GetChar(uint32_t index, char* result) { return false; }

bool ResultSetSQL::GetInt16(uint32_t index, int16_t* result) {
    if (result == NULL) {
        LOG(WARNING) << "input ptr is null pointer";
        return false;
    }
    int32_t ret = row_view_->GetInt16(index, result);
    return ret == 0;
}

bool ResultSetSQL::GetInt32(uint32_t index, int32_t* result) {
    if (result == NULL) {
        LOG(WARNING) << "input ptr is null pointer";
        return false;
    }
    int32_t ret = row_view_->GetInt32(index, result);
    return ret == 0;
}

bool ResultSetSQL::GetInt64(uint32_t index, int64_t* result) {
    if (result == NULL) {
        LOG(WARNING) << "input ptr is null pointer";
        return false;
    }
    int32_t ret = row_view_->GetInt64(index, result);
    return ret == 0;
}

bool ResultSetSQL::GetFloat(uint32_t index, float* result) {
    if (result == NULL) {
        LOG(WARNING) << "input ptr is null pointer";
        return false;
    }
    int32_t ret = row_view_->GetFloat(index, result);
    return ret == 0;
}

bool ResultSetSQL::GetDouble(uint32_t index, double* result) {
    if (result == NULL) {
        LOG(WARNING) << "input ptr is null pointer";
        return false;
    }
    int32_t ret = row_view_->GetDouble(index, result);
    return ret == 0;
}

bool ResultSetSQL::GetDate(uint32_t index, int32_t* date) {
    if (date == NULL) {
        LOG(WARNING) << "input ptr is null pointer";
        return false;
    }
    int32_t ret = row_view_->GetDate(index, date);
    return ret == 0;
}

bool ResultSetSQL::GetDate(uint32_t index, int32_t* year, int32_t* month,
                           int32_t* day) {
    if (day == NULL) {
        LOG(WARNING) << "input ptr is null pointer";
        return false;
    }
    return 0 == row_view_->GetDate(index, year, month, day);
}

bool ResultSetSQL::GetTime(uint32_t index, int64_t* mills) {
    if (mills == NULL) {
        LOG(WARNING) << "input ptr is null pointer";
        return false;
    }
    int32_t ret = row_view_->GetTimestamp(index, mills);
    return ret == 0;
}

}  // namespace sdk
}  // namespace rtidb