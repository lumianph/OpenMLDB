package com._4paradigm.sql.sdk;

import com._4paradigm.sql.*;
import com._4paradigm.sql.sdk.impl.BatchCallablePreparedStatementImpl;
import com._4paradigm.sql.sdk.impl.CallablePreparedStatementImpl;

import java.sql.PreparedStatement;
import java.sql.SQLException;
import java.util.List;

public interface SqlExecutor {
    boolean createDB(String db);
    boolean dropDB(String db);
    boolean executeDDL(String db, String sql);
    boolean executeInsert(String db, String sql);
    boolean executeInsert(String db, String sql, SQLInsertRow row);
    boolean executeInsert(String db, String sql, SQLInsertRows rows);
    ResultSet executeSQL(String db, String sql);
    SQLInsertRow getInsertRow(String db, String sql);
    PreparedStatement getInsertPreparedStmt(String db, String sql);
    PreparedStatement getRequestPreparedStmt(String db, String sql) throws SQLException;
    PreparedStatement getBatchRequestPreparedStmt(String db, String sql,
                                                  List<Integer> commonColumnIndices) throws SQLException;
    CallablePreparedStatementImpl getCallablePreparedStmt(String db, String spName) throws SQLException;
    BatchCallablePreparedStatementImpl getCallablePreparedStmtBatch(String db, String spName) throws SQLException;
    SQLInsertRows getInsertRows(String db, String sql);
    ResultSet executeSQL(String db, String sql, SQLRequestRow row);

    Schema getInputSchema(String dbName, String sql) throws SQLException;
    ProcedureInfo showProcedure(String dbName, String proName) throws SQLException;
}