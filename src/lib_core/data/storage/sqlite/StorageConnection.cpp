#include "StorageConnection.h"

#include <filesystem>
#include <format>
#include <string>
#include <system_error>

#include "BorrowedSqliteConnection.h"
#ifndef SRCTRL_MODULE_BUILD
#include "FileSystem.h"
#endif
#include "logging.h"

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.file;
#endif

namespace
{
// Why the database file cannot be a database, as the file system says it: SQLite's
// SQLITE_CANTOPEN names no reason. Empty when the path itself is no obstacle -- a missing file is
// created, and whatever else SQLite has to say about the file it says itself.
std::string unopenableReason(const FilePath& dbFilePath)
{
	std::error_code ec;
	const std::filesystem::file_status status = std::filesystem::status(dbFilePath.getPath(), ec);
	if (ec && ec != std::errc::no_such_file_or_directory)
	{
		return ec.message();	// a symlink loop, a name too long, a parent without permission
	}
	if (std::filesystem::is_directory(status))
	{
		return "it is a directory";
	}
	return {};
}
}	 // namespace

StorageConnection::StorageConnection(const FilePath& dbFilePath)
	: m_dbFilePath(dbFilePath.getCanonical())
{
	if (const std::string reason = unopenableReason(m_dbFilePath); !reason.empty())
	{
		const std::string message =
			std::format("Cannot open the database file \"{}\": {}", m_dbFilePath.str(), reason);
		LOG_ERROR(message);
		throw CppSQLite3Exception(SQLITE_CANTOPEN, message);
	}

	if (!m_dbFilePath.getParentDirectory().empty() && !m_dbFilePath.getParentDirectory().exists())
	{
		if (const auto created = FileSystem::createDirectories(m_dbFilePath.getParentDirectory()); !created)
		{
			LOG_ERROR(std::format(
				"Could not create the database directory \"{}\": {}",
				m_dbFilePath.getParentDirectory().str(),
				created.error().message()));
		}
	}

	try
	{
		m_database.open(m_dbFilePath.str());
	}
	catch (CppSQLite3Exception& e)
	{
		LOG_ERROR(
			"Failed to load database file \"" + m_dbFilePath.str() +
			"\" with message: " + e.errorMessage());
		throw;
	}

	// Borrow the now-open handle as a sqlpp23 connection: the typed view shares
	// this connection and transaction scope with the raw view.
	m_sqlpp = std::make_unique<sourcetrail::storage::BorrowedSqliteConnection>(m_database.handle());
}

StorageConnection::~StorageConnection()
{
	try
	{
		m_database.close();
	}
	catch (CppSQLite3Exception& e)
	{
		LOG_ERROR(e.errorMessage());
	}
}

StorageDb& StorageConnection::legacy()
{
	return m_database;
}

sourcetrail::storage::BorrowedSqliteConnection& StorageConnection::typed()
{
	return *m_sqlpp;
}

const FilePath& StorageConnection::dbFilePath() const
{
	return m_dbFilePath;
}
