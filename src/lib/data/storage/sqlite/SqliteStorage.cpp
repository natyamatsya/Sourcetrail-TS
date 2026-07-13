#include "SqliteStorage.h"

#include <sqlpp23/sqlpp23.h>

#include "BorrowedSqliteConnection.h"
#include "MetaTable.h"
#include "StorageConnection.h"
#include "TimeStamp.h"
#include "logging.h"

namespace
{
constexpr meta::Meta metaTable;

// Minimal typed model of SQLite's system catalog so hasTable() binds the table
// name instead of concatenating it into SQL. Only `type` and `name` are needed.
struct SqliteMaster_
{
	struct Type
	{
		SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(type, type);
		using data_type = std::optional<::sqlpp::text>;
		using has_default = std::true_type;
	};
	struct Name
	{
		SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(name, name);
		using data_type = std::optional<::sqlpp::text>;
		using has_default = std::true_type;
	};
	SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(sqlite_master, sqliteMaster);
	template <typename T>
	using _table_columns = sqlpp::table_columns<T, Type, Name>;
	using _required_insert_columns = sqlpp::detail::type_set<>;
};
constexpr ::sqlpp::table_t<SqliteMaster_> sqliteMaster;

// Read a nullable text column into std::string ("" when NULL) — matches the old
// getStringField(col, "") default.
template <typename Field>
std::string fieldText(const Field& field)
{
	return field.has_value() ? std::string(field.value()) : std::string();
}
}	 // namespace

SqliteStorage::SqliteStorage(StorageConnection& connection)
	: m_database(connection.legacy())
	, m_dbFilePath(connection.dbFilePath())
	, m_connection(connection)
{
	enablePragmas();
}

sourcetrail::storage::BorrowedSqliteConnection& SqliteStorage::db() const
{
	return m_connection.typed();
}

SqliteStorage::~SqliteStorage() = default;

void SqliteStorage::setup()
{
	enablePragmas();

	setupMetaTable();

	if (isEmpty() || !isIncompatible())
	{
		setupTables();

		if (!m_precompiledStatementsInitialized)
		{
			setupPrecompiledStatements();
			m_precompiledStatementsInitialized = true;
		}
	}
}

void SqliteStorage::clear()
{
	disablePragmas();

	clearMetaTable();
	clearTables();

	setup();
}

size_t SqliteStorage::getVersion() const
{
	std::string storageVersionStr = getMetaValue("storage_version");

	if (!storageVersionStr.empty())
	{
		return std::stoi(storageVersionStr);
	}

	return 0;
}

void SqliteStorage::setVersion(size_t version)
{
	insertOrUpdateMetaValue("storage_version", std::to_string(version));
}

void SqliteStorage::beginTransaction()
{
	try
	{
		db().start_transaction();
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
}

void SqliteStorage::commitTransaction()
{
	try
	{
		db().commit_transaction();
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
}

void SqliteStorage::rollbackTransaction()
{
	try
	{
		db().rollback_transaction();
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
}

void SqliteStorage::optimizeMemory() const
{
	executeStatement("VACUUM;");
}

FilePath SqliteStorage::getDbFilePath() const
{
	return m_dbFilePath;
}

bool SqliteStorage::isEmpty() const
{
	return getVersion() <= 0;
}

bool SqliteStorage::isIncompatible() const
{
	size_t storageVersion = getVersion();
	return isEmpty() || storageVersion != getStaticVersion();
}

void SqliteStorage::setTime()
{
	insertOrUpdateMetaValue("timestamp", TimeStamp::now().toString());
}

TimeStamp SqliteStorage::getTime() const
{
	return TimeStamp(getMetaValue("timestamp"));
}

void SqliteStorage::setupMetaTable()
{
	try
	{
		m_database.execDML(
			"CREATE TABLE IF NOT EXISTS meta("
			"id INTEGER, "
			"key TEXT, "
			"value TEXT, "
			"PRIMARY KEY(id)"
			");");
	}
	catch (CppSQLite3Exception& e)
	{
		LOG_ERROR(std::to_string(e.errorCode()) + ": " + e.errorMessage());

		throw(std::exception());
	}
}

void SqliteStorage::clearMetaTable()
{
	try
	{
		m_database.execDML("DROP TABLE IF EXISTS main.meta;");
	}
	catch (CppSQLite3Exception& e)
	{
		LOG_ERROR(std::to_string(e.errorCode()) + ": " + e.errorMessage());
	}
}

bool SqliteStorage::executeStatement(const std::string& statement) const
{
	try
	{
		m_database.execDML(statement);
	}
	catch (CppSQLite3Exception &e)
	{
		LOG_ERROR(std::to_string(e.errorCode()) + ": " + e.errorMessage());
		return false;
	}
	return true;
}

bool SqliteStorage::executeStatement(StorageStmt& statement) 
{
	try
	{
		statement.execDML();
	}
	catch (CppSQLite3Exception &e)
	{
		LOG_ERROR(std::to_string(e.errorCode()) + ": " + e.errorMessage());
		return false;
	}

	statement.reset();
	return true;
}

long long SqliteStorage::executeStatementScalar(const std::string& statement, const int nullValue) const
{
	long long ret = 0;
	try
	{
		ret = m_database.execScalar(statement, nullValue);
	}
	catch (CppSQLite3Exception &e)
	{
		LOG_ERROR(std::to_string(e.errorCode()) + ": " + e.errorMessage());
	}
	return ret;
}

long long SqliteStorage::executeStatementScalar(StorageStmt& statement, const int nullValue) 
{
	long long ret = 0;
	try
	{
		StorageQuery q = executeQuery(statement);

		if (q.eof() || q.numFields() < 1)
		{
			throw CppSQLite3Exception(CPPSQLITE_ERROR, "Invalid scalar query");
		}

		ret = q.getIntField(0, nullValue);
	}
	catch (CppSQLite3Exception &e)
	{
		LOG_ERROR(std::to_string(e.errorCode()) + ": " + e.errorMessage());
	}

	return ret;
}

StorageQuery SqliteStorage::executeQuery(const std::string& statement) const
{
	try
	{
		return m_database.execQuery(statement);
	}
	catch (CppSQLite3Exception &e)
	{
		LOG_ERROR(std::to_string(e.errorCode()) + ": " + e.errorMessage());
	}
	return StorageQuery();
}

StorageQuery SqliteStorage::executeQuery(StorageStmt& statement) 
{
	try
	{
		return statement.execQuery();
	}
	catch (CppSQLite3Exception &e)
	{
		LOG_ERROR(std::to_string(e.errorCode()) + ": " + e.errorMessage());
	}
	return StorageQuery();
}

bool SqliteStorage::hasTable(const std::string& tableName) const
{
	using namespace sqlpp;
	try
	{
		for (const auto& row: db()(select(sqliteMaster.name)
									   .from(sqliteMaster)
									   .where(sqliteMaster.type == "table" and sqliteMaster.name == tableName)))
		{
			if (fieldText(row.name) == tableName)
			{
				return true;
			}
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return false;
}

std::string SqliteStorage::getMetaValue(const std::string& key) const
{
	using namespace sqlpp;
	if (hasTable("meta"))
	{
		try
		{
			for (const auto& row: db()(select(metaTable.value).from(metaTable).where(metaTable.key == key)))
			{
				return fieldText(row.value);
			}
		}
		catch (const std::exception& e)
		{
			LOG_ERROR(e.what());
		}
	}
	return "";
}

void SqliteStorage::insertOrUpdateMetaValue(const std::string& key, const std::string& value)
{
	using namespace sqlpp;
	// The raw path faked an upsert with INSERT OR REPLACE + a (SELECT id ...)
	// subquery, because meta has no UNIQUE(key). The typed equivalent is a plain
	// read-then-update-or-insert.
	try
	{
		bool exists = false;
		for (const auto& row: db()(select(metaTable.id).from(metaTable).where(metaTable.key == key)))
		{
			static_cast<void>(row);
			exists = true;
			break;
		}
		if (exists)
		{
			db()(update(metaTable).set(metaTable.value = value).where(metaTable.key == key));
		}
		else
		{
			db()(insert_into(metaTable).set(metaTable.key = key, metaTable.value = value));
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
}

void SqliteStorage::enablePragmas() const
{
	executeStatement("PRAGMA FOREIGN_KEYS=ON;");
	executeStatement("PRAGMA JOURNAL_MODE=WAL;");
	executeStatement("PRAGMA SYNCHRONOUS=NORMAL;");
	executeStatement("PRAGMA MMAP_SIZE=268435456;");
	executeStatement("PRAGMA CACHE_SIZE=-65536;");
}

void SqliteStorage::disablePragmas() const
{
	executeStatement("PRAGMA FOREIGN_KEYS=OFF;");
}

void SqliteStorage::setBulkWritePragmas(bool enabled) const
{
	// For the throwaway indexing target: skip the fsync per committed transaction.
	// An app crash cannot corrupt the DB with SYNCHRONOUS=OFF (SQLite guarantee);
	// only an OS/power failure can -- and the temp DB is discarded and indexing
	// restarted in that case anyway. Restore NORMAL before the DB becomes the
	// live one (TaskFinishParsing, before optimizeMemory/swap).
	executeStatement(enabled ? "PRAGMA SYNCHRONOUS=OFF;" : "PRAGMA SYNCHRONOUS=NORMAL;");
}
