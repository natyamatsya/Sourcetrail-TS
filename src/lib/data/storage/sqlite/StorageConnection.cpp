#include "StorageConnection.h"

#include "BorrowedSqliteConnection.h"
#include "FileSystem.h"
#include "logging.h"

StorageConnection::StorageConnection(const FilePath& dbFilePath)
	: m_dbFilePath(dbFilePath.getCanonical())
{
	if (!m_dbFilePath.getParentDirectory().empty() && !m_dbFilePath.getParentDirectory().exists())
	{
		FileSystem::createDirectories(m_dbFilePath.getParentDirectory());
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
