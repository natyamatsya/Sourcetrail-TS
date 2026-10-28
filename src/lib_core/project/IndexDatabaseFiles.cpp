#include "IndexDatabaseFiles.h"

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

#include <sqlite3.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "FilePath.h"
#include "logging.h"

namespace
{
std::filesystem::path besides(const FilePath& db, const char* suffix)
{
	std::filesystem::path path = db.getPath();
	path += suffix;
	return path;
}

std::filesystem::path writeAheadLog(const FilePath& db)
{
	return besides(db, "-wal");
}

bool fileExists(const std::filesystem::path& path)
{
	std::error_code error;
	return std::filesystem::exists(path, error);
}

// Closed however the function that opened it ends. Closing the last connection to a
// database merges its write-ahead log into it and removes the log.
struct Connection
{
	sqlite3* handle = nullptr;

	Connection() = default;
	Connection(const Connection&) = delete;
	Connection& operator=(const Connection&) = delete;
	~Connection()
	{
		sqlite3_close(handle);
	}

	// Without creating the file.
	bool open(const FilePath& db)
	{
		if (sqlite3_open_v2(db.str().c_str(), &handle, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK)
		{
			return false;
		}
		sqlite3_busy_timeout(handle, 2000);	// a connection of the run that is still closing
		return true;
	}
};

// Finalized however the function that prepared it ends.
struct Statement
{
	sqlite3_stmt* handle = nullptr;

	Statement(sqlite3* connection, const char* sql)
	{
		if (sqlite3_prepare_v2(connection, sql, -1, &handle, nullptr) != SQLITE_OK)
		{
			handle = nullptr;
		}
	}
	Statement(const Statement&) = delete;
	Statement& operator=(const Statement&) = delete;
	~Statement()
	{
		sqlite3_finalize(handle);
	}
};

// The storage version `db` says it has; nullopt when it is no index database.
std::optional<std::size_t> storageVersionOf(const FilePath& db)
{
	Connection connection;
	if (!connection.open(db))
	{
		return std::nullopt;
	}
	sqlite3_stmt* statement = nullptr;
	if (sqlite3_prepare_v2(
			connection.handle, "SELECT value FROM meta WHERE key = 'storage_version'", -1, &statement, nullptr) !=
		SQLITE_OK)
	{
		return std::nullopt;
	}
	std::optional<std::size_t> version;
	if (sqlite3_step(statement) == SQLITE_ROW)
	{
		if (const unsigned char* text = sqlite3_column_text(statement, 0))
		{
			version = std::strtoull(reinterpret_cast<const char*>(text), nullptr, 10);
		}
	}
	sqlite3_finalize(statement);
	return version;
}
}	 // namespace

namespace index_database
{
std::expected<void, IndexSwapError> check(const FilePath& db, std::size_t storageVersion)
{
	if (!fileExists(db.getPath()))
	{
		return std::unexpected(IndexSwapError::TempIndexMissing);
	}
	if (storageVersionOf(db) != storageVersion)
	{
		return std::unexpected(IndexSwapError::TempIndexInvalid);
	}
	// A log left after the close: a connection still writes it, and the file alone
	// is not all of the index.
	if (fileExists(writeAheadLog(db)))
	{
		return std::unexpected(IndexSwapError::TempIndexInUse);
	}
	return {};
}

std::expected<void, IndexSwapError> replace(
	const FilePath& indexDb, const FilePath& tempIndexDb, std::size_t storageVersion)
{
	if (const std::expected<void, IndexSwapError> checked = check(tempIndexDb, storageVersion); !checked)
	{
		if (checked.error() == IndexSwapError::TempIndexMissing)
		{
			remove(tempIndexDb);
		}
		return checked;
	}

	if (fileExists(indexDb.getPath()))
	{
		// The index's log goes with its last connection. One left belongs to a
		// connection still open (another process browsing it): renamed over, the file
		// would be read with that log, a different database's pages.
		{
			// A read: SQLite opens the log only then (and recovers one a crash left).
			Connection connection;
			if (connection.open(indexDb))
			{
				sqlite3_exec(connection.handle, "SELECT count(*) FROM sqlite_master", nullptr, nullptr, nullptr);
			}
		}
		if (fileExists(writeAheadLog(indexDb)))
		{
			return std::unexpected(IndexSwapError::IndexInUse);
		}
	}

	// One rename, which replaces the index at once: there is always an index, the old
	// one or the new one.
	std::error_code error;
	std::filesystem::rename(tempIndexDb.getPath(), indexDb.getPath(), error);
	indexDb.recheckExists();
	tempIndexDb.recheckExists();
	if (error)
	{
		LOG_ERROR(
			"Cannot rename " + tempIndexDb.str() + " to " + indexDb.str() + ": " + error.message());
		return std::unexpected(IndexSwapError::ReplaceFailed);
	}
	return {};
}

std::optional<std::string> nameProblem(const FilePath& indexDb, const FilePath& tempIndexDb)
{
	// The longest name SQLite makes beside them: the temporary database's rollback journal.
	const std::string longest = tempIndexDb.fileName() + "-journal";
	long limit = 255;
#ifndef _WIN32
	const std::filesystem::path directory = indexDb.getPath().parent_path();
	if (const long found = ::pathconf(directory.empty() ? "." : directory.c_str(), _PC_NAME_MAX); found > 0)
	{
		limit = found;
	}
#endif
	if (static_cast<long>(longest.size()) <= limit)
	{
		return std::nullopt;
	}
	// "<name>.srctrl.db": what the name may have, with what SQLite adds to it.
	const std::size_t nameSize = indexDb.withoutExtension().withoutExtension().fileName().size();
	const long nameLimit = limit - static_cast<long>(longest.size() - nameSize);
	return "The project's name is too long for its index files: \"" + longest + "\" has " +
		std::to_string(longest.size()) + " bytes, and the file system allows " + std::to_string(limit) +
		". Give the project a name of at most " + std::to_string(nameLimit) + " bytes.";
}

std::optional<IndexingRunMark> runMark(const FilePath& db)
{
	if (!fileExists(db.getPath()))
	{
		return std::nullopt;
	}
	Connection connection;
	if (!connection.open(db))
	{
		return std::nullopt;
	}
	const Statement statement(connection.handle, "SELECT value FROM meta WHERE key = ?1");
	if (statement.handle == nullptr)
	{
		return std::nullopt;
	}
	sqlite3_bind_text(statement.handle, 1, runMarkKey, -1, SQLITE_STATIC);
	if (sqlite3_step(statement.handle) != SQLITE_ROW)
	{
		return std::nullopt;
	}
	const char* text = reinterpret_cast<const char*>(sqlite3_column_text(statement.handle, 0));
	const std::string_view value = text != nullptr ? text : "";
	for (const IndexingRunMark mark: {IndexingRunMark::Running, IndexingRunMark::Stopped, IndexingRunMark::Finished})
	{
		if (value == to_std_sv(mark))
		{
			return mark;
		}
	}
	return std::nullopt;
}

void markStopped(const FilePath& db)
{
	if (!fileExists(db.getPath()))
	{
		return;
	}
	Connection connection;
	if (!connection.open(db))
	{
		return;
	}
	const Statement statement(connection.handle, "UPDATE meta SET value = ?1 WHERE key = ?2 AND value = ?3");
	if (statement.handle == nullptr)
	{
		return;
	}
	const std::string_view stopped = to_std_sv(IndexingRunMark::Stopped);
	const std::string_view running = to_std_sv(IndexingRunMark::Running);
	sqlite3_bind_text(statement.handle, 1, stopped.data(), static_cast<int>(stopped.size()), SQLITE_STATIC);
	sqlite3_bind_text(statement.handle, 2, runMarkKey, -1, SQLITE_STATIC);
	sqlite3_bind_text(statement.handle, 3, running.data(), static_cast<int>(running.size()), SQLITE_STATIC);
	if (sqlite3_step(statement.handle) != SQLITE_DONE)
	{
		LOG_WARNING("Cannot mark the indexing data of " + db.str() + " stopped: " + sqlite3_errmsg(connection.handle));
	}
}

void remove(const FilePath& db)
{
	for (const std::filesystem::path& path:
		 {db.getPath(), writeAheadLog(db), besides(db, "-shm"), besides(db, "-journal")})
	{
		std::error_code error;
		std::filesystem::remove(path, error);
	}
	db.recheckExists();
}
}	 // namespace index_database
