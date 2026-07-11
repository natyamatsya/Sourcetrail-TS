/*
 * turso_shim.h — hand-written C declarations for the `turso_shim` Rust staticlib.
 *
 * These `tsq_*` functions are a firewalled subset of the sqlite3 C API (only the
 * primitives Sourcetrail's CppSQLite3 wrapper uses), re-exported over turso_core
 * so Turso can be linked alongside real SQLite without symbol collisions.
 *
 * Result codes and column-type codes intentionally match sqlite3.h.
 * Kept in sync by hand with src/turso_shim/src/lib.rs (the surface is tiny).
 */
#ifndef TURSO_SHIM_H
#define TURSO_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Result codes (mirror sqlite3.h). */
#define TSQ_OK 0
#define TSQ_ERROR 1
#define TSQ_ROW 100
#define TSQ_DONE 101

/* Column types (mirror sqlite3.h). */
#define TSQ_INTEGER 1
#define TSQ_FLOAT 2
#define TSQ_TEXT 3
#define TSQ_BLOB 4
#define TSQ_NULL 5

/* Opaque handles owned by the Rust side. */
typedef struct TsqDb TsqDb;
typedef struct TsqStmt TsqStmt;
typedef struct TsqConn TsqConn;  /* additional connection for concurrent MVCC writers */

/* Last error message for the calling thread; never null. Copy immediately. */
const char* tsq_last_error(void);

/* Connection lifecycle. Returns null on failure (see tsq_last_error). */
TsqDb* tsq_open(const char* path);
void tsq_close(TsqDb* db);

/* Run one or more ';'-separated statements to completion (like sqlite3_exec). */
int tsq_exec(TsqDb* db, const char* sql);

/* Prepared statements. tsq_prepare returns null on failure. */
TsqStmt* tsq_prepare(TsqDb* db, const char* sql);
int tsq_bind_int(TsqStmt* stmt, int idx, long long val);
int tsq_bind_text(TsqStmt* stmt, int idx, const char* ptr, int len);
int tsq_step(TsqStmt* stmt);   /* TSQ_ROW / TSQ_DONE / TSQ_ERROR */
int tsq_reset(TsqStmt* stmt);
void tsq_finalize(TsqStmt* stmt);

/* Column access, valid after tsq_step returns TSQ_ROW, until the next
 * step/reset/finalize. tsq_column_text returns null for SQL NULL. */
int tsq_column_count(TsqStmt* stmt);
int tsq_column_type(TsqStmt* stmt, int i);
long long tsq_column_int(TsqStmt* stmt, int i);
const char* tsq_column_text(TsqStmt* stmt, int i);

long long tsq_last_insert_rowid(TsqDb* db);
long long tsq_changes(TsqDb* db);
void tsq_interrupt(TsqDb* db);

/* Additional connections on a shared Database, for concurrent MVCC writers.
 * With `PRAGMA journal_mode = 'mvcc'` set on the database, several connections
 * can each hold a `BEGIN CONCURRENT` write transaction simultaneously.
 * Statements from tsq_conn_prepare use the same tsq_bind/step/column API. */
TsqConn* tsq_new_connection(TsqDb* db);
void tsq_conn_close(TsqConn* conn);
int tsq_conn_exec(TsqConn* conn, const char* sql);
TsqStmt* tsq_conn_prepare(TsqConn* conn, const char* sql);
long long tsq_conn_last_insert_rowid(TsqConn* conn);
long long tsq_conn_changes(TsqConn* conn);

#ifdef __cplusplus
}
#endif

#endif /* TURSO_SHIM_H */
