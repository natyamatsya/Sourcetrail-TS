#ifndef STORAGE_DB_TYPES_H
#define STORAGE_DB_TYPES_H

// Selects the database backend used by the SQLite storage layer.
//
// Normally these alias the real CppSQLite3 types. When SOURCETRAIL_TURSO_COMPARE
// is defined they alias the Dual* comparison types, which run SQLite and Turso
// side by side and report any divergence. The storage code refers to the backend
// only through StorageDb / StorageStmt / StorageQuery, so switching backends is a
// compile-time choice with zero cost when the option is off.
//
// CppSQLite3Exception is thrown by both backends' error paths and is referenced
// directly by the storage layer, so it is always pulled in via CppSQLite3.h.

#include "CppSQLite3.h"

#ifdef SOURCETRAIL_TURSO_COMPARE
#include "DualSqliteDatabase.h"

using StorageDb = DualCppSqlite3DB;
using StorageStmt = DualStatement;
using StorageQuery = DualQuery;
#else
using StorageDb = CppSQLite3DB;
using StorageStmt = CppSQLite3Statement;
using StorageQuery = CppSQLite3Query;
#endif

#endif	// STORAGE_DB_TYPES_H
