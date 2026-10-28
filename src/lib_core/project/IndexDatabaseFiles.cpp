#include "IndexDatabaseFiles.h"

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

#include <sqlite3.h>

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
		return sqlite3_open_v2(db.str().c_str(), &handle, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK;
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
			Connection connection;
			connection.open(indexDb);
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
