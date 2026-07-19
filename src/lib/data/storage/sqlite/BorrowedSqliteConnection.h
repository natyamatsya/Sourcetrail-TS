#ifndef BORROWED_SQLITE_CONNECTION_H
#define BORROWED_SQLITE_CONNECTION_H

// A sqlpp23 SQLite connection that does NOT own its sqlite3 handle.
//
// This is the seam that lets the typed (sqlpp23) view and the legacy CppSQLite3DB
// view share ONE sqlite3 handle, so raw SQL and typed queries run in the same
// transaction scope during the incremental migration. See
// context/DESIGN_SQLPP23_MIGRATION.md ("one handle, two views").
//
// Phase 0: nothing consumes this yet. It is validated standalone; Phase 1 wires
// sqlpp23 into the main build and hands it CppSQLite3DB::handle().
//
// Mechanism: sqlpp23's sqlite3 connection owns its handle via
//   std::unique_ptr<::sqlite3, int(*)(::sqlite3*)>
// with a function-pointer deleter. We build the connection's handle manually with
// a no-op deleter, so destroying the borrowed connection leaves the real handle
// (owned by CppSQLite3DB) untouched. common_connection<connection_base> exposes the
// full public query API and inherits the protected handle constructor we need.

#include <memory>

#include <sqlite3.h>
// sqlpp23 include-only seam: this wrapper derives from common_connection<connection_base> and touches
// connection_handle -- sqlpp23 *internals* that its C++20 module does NOT export (only the high-level
// `connection`/`connection_config` + the query DSL are exported). So even under SOURCETRAIL_SQLPP23_MODULES
// this header stays a textual #include, and the impl TUs import sqlpp23 for the DSL alongside it (the two
// coexist cleanly). See DESIGN_STORAGE_MODULARIZATION.md §3.
#include <sqlpp23/sqlite3/database/connection.h>
#include <sqlpp23/sqlite3/database/connection_config.h>

namespace sourcetrail::storage
{

class BorrowedSqliteConnection: public sqlpp::common_connection<sqlpp::sqlite3::connection_base>
{
	using Base = sqlpp::common_connection<sqlpp::sqlite3::connection_base>;

public:
	// `handle` remains owned elsewhere (CppSQLite3DB); this connection borrows it.
	explicit BorrowedSqliteConnection(::sqlite3 *handle): Base(makeBorrowedHandle(handle)) {}

private:
	static int noopClose(::sqlite3 * /*handle*/) noexcept
	{
		return SQLITE_OK;	// borrowed: the owner closes the real handle
	}

	static sqlpp::sqlite3::detail::connection_handle makeBorrowedHandle(::sqlite3 *handle)
	{
		sqlpp::sqlite3::detail::connection_handle borrowed;
		// A non-null config keeps connection_base::debug() safe if debug logging is
		// ever enabled; the path is irrelevant since we never (re)open.
		borrowed.config = std::make_shared<sqlpp::sqlite3::connection_config>();
		borrowed.sqlite = std::unique_ptr<::sqlite3, int (*)(::sqlite3 *)>(handle, &noopClose);
		return borrowed;
	}
};

}	 // namespace sourcetrail::storage

#endif	  // BORROWED_SQLITE_CONNECTION_H
