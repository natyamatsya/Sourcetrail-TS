#ifndef SQLITE_STORAGE_H
#define SQLITE_STORAGE_H

#include <memory>

#include "StorageDbTypes.h"

#include "FilePath.h"
#include "SqliteDatabaseIndex.h"

class TimeStamp;

// Typed (sqlpp23) view over the raw handle; kept forward-declared so this header
// stays free of sqlpp23 includes. Defined in BorrowedSqliteConnection.h, which
// only SqliteStorage.cpp (and later, migrated subclass .cpp files) include.
namespace sourcetrail::storage
{
class BorrowedSqliteConnection;
}

class SqliteStorage
{
public:
	SqliteStorage(const FilePath& dbFilePath);
	virtual ~SqliteStorage();

	void setup();

	//! Toggle write-heavy indexing pragmas (currently SYNCHRONOUS=OFF) on the
	//! throwaway indexing target; restore before the DB becomes the live one.
	void setBulkWritePragmas(bool enabled) const;
	void clear();

	size_t getVersion() const;
	void setVersion(size_t version);

	void beginTransaction();
	void commitTransaction();
	void rollbackTransaction();

	void optimizeMemory() const;

	FilePath getDbFilePath() const;

	bool isEmpty() const;
	bool isIncompatible() const;

	void setTime();
	TimeStamp getTime() const;

protected:
	void setupMetaTable();
	void clearMetaTable();

	bool executeStatement(const std::string& statement) const;
	static bool executeStatement(StorageStmt& statement);
	long long executeStatementScalar(const std::string& statement, const int nullValue = 0) const;
	static long long executeStatementScalar(StorageStmt& statement, const int nullValue = 0);
	StorageQuery executeQuery(const std::string& statement) const;
	static StorageQuery executeQuery(StorageStmt& statement);

	bool hasTable(const std::string& tableName) const;

	std::string getMetaValue(const std::string& key) const;
	void insertOrUpdateMetaValue(const std::string& key, const std::string& value);

	// Typed (sqlpp23) view over m_database's handle: the SAME connection and
	// transaction scope as the raw path above. Phase 1 exposes this; no caller
	// uses it yet. See context/DESIGN_SQLPP23_MIGRATION.md.
	sourcetrail::storage::BorrowedSqliteConnection& db();

	mutable StorageDb m_database;
	FilePath m_dbFilePath;

private:
	virtual size_t getStaticVersion() const = 0;
	virtual void clearTables() = 0;
	virtual void setupTables() = 0;
	virtual void setupPrecompiledStatements() = 0;

	void enablePragmas() const;
	void disablePragmas() const;

	std::vector<std::pair<int, SqliteDatabaseIndex>> m_indices;

	bool m_precompiledStatementsInitialized = false;

	// Borrowed sqlpp23 connection over m_database's handle (non-owning). Created in
	// the constructor once the handle is open; destroyed before m_database (its
	// deleter is a no-op, so ordering is not load-bearing).
	std::unique_ptr<sourcetrail::storage::BorrowedSqliteConnection> m_sqlpp;
};

#endif	  // SQLITE_STORAGE_H
