#ifndef STORAGE_CONNECTION_H
#define STORAGE_CONNECTION_H

#include <memory>

#include "StorageDbTypes.h"

#include "FilePath.h"

// Typed (sqlpp23) view over the raw handle; kept forward-declared so this header
// stays free of sqlpp23 includes.
namespace sourcetrail::storage
{
class BorrowedSqliteConnection;
}

//! Owns the ONE sqlite3 handle behind a storage database file and exposes both
//! views onto it: the legacy CppSQLite3 API (raw SQL) and the typed sqlpp23
//! connection. Both views share the connection — and therefore the transaction
//! scope, PRAGMAs and WAL state. The composition root (PersistentStorage, or a
//! test) owns the connection; SqliteStorage instances only borrow it, which
//! keeps handle lifetime explicit around the temp-DB swap.
//! See context/DESIGN_SQLPP23_MIGRATION.md ("one handle, two views").
class StorageConnection
{
public:
	//! Opens the database (creating parent directories as needed).
	//! Throws CppSQLite3Exception when the file cannot be opened.
	explicit StorageConnection(const FilePath& dbFilePath);
	~StorageConnection();

	StorageConnection(const StorageConnection&) = delete;
	StorageConnection& operator=(const StorageConnection&) = delete;

	//! Raw view: CppSQLite3DB (or the Dual Turso-compare wrapper).
	StorageDb& legacy();

	//! Typed view: borrowed sqlpp23 connection over legacy()'s SQLite handle.
	sourcetrail::storage::BorrowedSqliteConnection& typed();

	const FilePath& dbFilePath() const;

private:
	FilePath m_dbFilePath;
	StorageDb m_database;

	// Borrowed sqlpp23 connection over m_database's handle (non-owning). Created
	// once the handle is open; its deleter is a no-op, so destruction order
	// relative to m_database is not load-bearing.
	std::unique_ptr<sourcetrail::storage::BorrowedSqliteConnection> m_sqlpp;
};

#endif	  // STORAGE_CONNECTION_H
