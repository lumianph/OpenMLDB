/*-------------------------------------------------------------------------
 * Copyright (C) 2019, 4paradigm
 * node_enum.h
 *
 * Author: chenjing
 * Date: 2019/10/29
 *--------------------------------------------------------------------------
 **/

#ifndef SRC_NODE_NODE_ENUM_H_
#define SRC_NODE_NODE_ENUM_H_

#include <proto/type.pb.h>
#include <string>
#include "proto/common.pb.h"
namespace fesql {
namespace node {

const char SPACE_ST[] = "+-";
const char SPACE_ED[] = "";
const char OR_INDENT[] = "|\t";
const char INDENT[] = " \t";

enum SQLNodeType {
    // SQL
    kSelectStmt = 0,
    kCreateStmt,
    kInsertStmt,
    kCmdStmt,
    kExpr,
    kType,
    kResTarget,
    kTable,
    kWindowFunc,
    kWindowDef,
    kFrameBound,
    kFrames,
    kColumnDesc,
    kColumnIndex,
    kIndexKey,
    kIndexTs,
    kIndexVersion,
    kIndexTTL,
    kName,
    kConst,
    kLimit,
    kOrderBy,

    kDesc,
    kAsc,

    kFrameRange,
    kFrameRows,

    kPreceding,
    kFollowing,
    kCurrent,

    kFn,
    kFnDef,
    kFnHeader,
    kFnValue,
    kFnIfElseBlock,
    kFnIfBlock,
    kFnElseBlock,
    kFnElifBlock,
    kFnForInBlock,
    kFnAssignStmt,
    kFnReturnStmt,
    kFnIfStmt,
    kFnElifStmt,
    kFnElseStmt,
    kFnForInStmt,
    kFnPara,
    kFnParaList,
    kFnList,
    kUnknow
};

enum ExprType {
    kExprBinary,
    kExprUnary,
    kExprIn,
    kExprCall,
    kExprCase,
    kExprCast,
    kExprId,
    kExprColumnRef,
    kExprPrimary,
    kExprList,
    kExprForIn,
    kExprRange,
    kExprAll,
    kExprStruct,
    kExprUnknow = 9999
};
// typedef fesql::type::Type DataType;
enum DataType {
    kBool,
    kInt16,
    kInt32,
    kInt64,
    kFloat,
    kDouble,
    kVarchar,
    kDate,
    kTimestamp,
    kList,
    kHour,
    kMinute,
    kSecond,
    kDay,
    kMap,
    kIterator,
    kInt8Ptr,
    kRow,
    kVoid = 100,
    kNull = 101
};

enum TimeUnit {
    kTimeUnitHour,
    kTimeUnitDay,
    kTimeUnitMinute,
    kTimeUnitSecond,
};
enum FnOperator {
    kFnOpAdd,
    kFnOpMinus,
    kFnOpMulti,
    kFnOpDiv,
    kFnOpFDiv,
    kFnOpMod,
    kFnOpAnd,
    kFnOpOr,
    kFnOpNot,
    kFnOpEq,
    kFnOpNeq,
    kFnOpLt,
    kFnOpLe,
    kFnOpGt,
    kFnOpGe,
    kFnOpDot,
    kFnOpAt,
    kFnOpBracket,
    kFnOpNone
};

enum CmdType {
    kCmdCreateGroup,
    kCmdCreateDatabase,
    kCmdSource,
    kCmdUseDatabase,
    kCmdShowDatabases,
    kCmdShowTables,
    kCmdDescTable,
    kCmdDropTable,
    kCmdExit
};

enum PlanType {
    kPlanTypeCmd,
    kPlanTypeFuncDef,
    kPlanTypeSelect,
    kPlanTypeCreate,
    kPlanTypeInsert,
    kPlanTypeScan,
    kPlanTypeMerge,
    kPlanTypeLimit,
    kPlanTypeFilter,
    kProjectList,
    kPlanTypeWindow,
    kProject,
    kScalarFunction,
    kOpExpr,
    kAggFunction,
    kAggWindowFunction,
    kUnknowPlan,

    kScanTypeSeqScan,
    kScanTypeIndexScan,
};

// batch plan node type
enum BatchPlanNodeType { kBatchDataset, kBatchPartition, kBatchMap };

}  // namespace node
}  // namespace fesql

#endif  // SRC_NODE_NODE_ENUM_H_
