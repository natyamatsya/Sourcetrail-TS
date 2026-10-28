#include "Catch2.hpp"

#include <fstream>
#include <optional>

#include "FilePath.h"
#include "FileSystem.h"
#include "IndexDatabaseFiles.h"
#include "SqliteIndexStorage.h"
#include "StorageConnection.h"
#include "TestTempDirectory.h"

namespace
{
// An index database of this version with `nodeCount` nodes, closed again.
void writeIndex(const FilePath& path, int nodeCount)
{
	StorageConnection connection(path);
	SqliteIndexStorage storage(connection);
	storage.setup();
	storage.setVersion(SqliteIndexStorage::getStorageVersion());
	storage.beginTransaction();
	for (int i = 0; i < nodeCount; i++)
	{
		storage.addNode(StorageNodeData(NODE_CLASS, "node" + std::to_string(i)));
	}
	storage.commitTransaction();
}

int nodeCount(const FilePath& path)
{
	StorageConnection connection(path);
	SqliteIndexStorage storage(connection);
	return storage.getNodeCount();
}

std::expected<void, IndexSwapError> replace(const FilePath& indexDb, const FilePath& tempIndexDb)
{
	return index_database::replace(indexDb, tempIndexDb, SqliteIndexStorage::getStorageVersion());
}
}	 // namespace

TEST_CASE("the index an indexing run wrote replaces the previous one")
{
	const TestTempDirectory directory {"IndexDatabaseFilesTestSuite"};
	const FilePath indexDb(directory.getFilePath("project.srctrl.db"));
	const FilePath tempIndexDb(directory.getFilePath("project.srctrl.db.tmp"));
	writeIndex(indexDb, 1);
	writeIndex(tempIndexDb, 3);

	REQUIRE(replace(indexDb, tempIndexDb));

	REQUIRE(nodeCount(indexDb) == 3);
	REQUIRE_FALSE(tempIndexDb.recheckExists());
}

TEST_CASE("the index an indexing run wrote becomes the index where there was none")
{
	const TestTempDirectory directory {"IndexDatabaseFilesTestSuite"};
	const FilePath indexDb(directory.getFilePath("project.srctrl.db"));
	const FilePath tempIndexDb(directory.getFilePath("project.srctrl.db.tmp"));
	writeIndex(tempIndexDb, 2);

	REQUIRE(replace(indexDb, tempIndexDb));

	REQUIRE(nodeCount(indexDb) == 2);
}

// The data loss: the run's database deleted while indexing (git clean -xfd) took the
// index with it, which was removed before the rename that failed.
TEST_CASE("an index whose replacement is gone stays as it was")
{
	const TestTempDirectory directory {"IndexDatabaseFilesTestSuite"};
	const FilePath indexDb(directory.getFilePath("project.srctrl.db"));
	const FilePath tempIndexDb(directory.getFilePath("project.srctrl.db.tmp"));
	writeIndex(indexDb, 4);
	// What the deleted database's connection left beside it.
	const FilePath tempLog(directory.getFilePath("project.srctrl.db.tmp-wal"));
	std::ofstream {tempLog.str()} << "frames of a database that is gone";

	const std::expected<void, IndexSwapError> replaced = replace(indexDb, tempIndexDb);

	REQUIRE_FALSE(replaced);
	REQUIRE(replaced.error() == IndexSwapError::TempIndexMissing);
	REQUIRE(nodeCount(indexDb) == 4);
	// The next run's database of that name must not read it.
	REQUIRE_FALSE(tempLog.recheckExists());
}

TEST_CASE("an index is not replaced by a file that is no index")
{
	const TestTempDirectory directory {"IndexDatabaseFilesTestSuite"};
	const FilePath indexDb(directory.getFilePath("project.srctrl.db"));
	const FilePath tempIndexDb(directory.getFilePath("project.srctrl.db.tmp"));
	writeIndex(indexDb, 4);

	SECTION("an empty file")
	{
		std::ofstream {tempIndexDb.str()};
	}
	SECTION("text")
	{
		std::ofstream {tempIndexDb.str()} << "not a database";
	}
	SECTION("a database of another version")
	{
		{
			StorageConnection connection(tempIndexDb);
			SqliteIndexStorage storage(connection);
			storage.setup();
			storage.setVersion(SqliteIndexStorage::getStorageVersion() - 1);
		}
	}

	const std::expected<void, IndexSwapError> replaced = replace(indexDb, tempIndexDb);

	REQUIRE_FALSE(replaced);
	REQUIRE(replaced.error() == IndexSwapError::TempIndexInvalid);
	REQUIRE(nodeCount(indexDb) == 4);
	REQUIRE(tempIndexDb.recheckExists());
}

TEST_CASE("an index open elsewhere is not replaced")
{
	const TestTempDirectory directory {"IndexDatabaseFilesTestSuite"};
	const FilePath indexDb(directory.getFilePath("project.srctrl.db"));
	const FilePath tempIndexDb(directory.getFilePath("project.srctrl.db.tmp"));
	writeIndex(indexDb, 1);
	writeIndex(tempIndexDb, 3);

	// A reader with the index open (its write-ahead log is there while it is).
	std::optional<StorageConnection> reader(std::in_place, indexDb);
	SqliteIndexStorage storage(*reader);
	REQUIRE(storage.getNodeCount() == 1);

	const std::expected<void, IndexSwapError> replaced = replace(indexDb, tempIndexDb);

	REQUIRE_FALSE(replaced);
	REQUIRE(replaced.error() == IndexSwapError::IndexInUse);
	REQUIRE(storage.getNodeCount() == 1);
	REQUIRE(tempIndexDb.recheckExists());
}

TEST_CASE("an index is not replaced by one still being written")
{
	const TestTempDirectory directory {"IndexDatabaseFilesTestSuite"};
	const FilePath indexDb(directory.getFilePath("project.srctrl.db"));
	const FilePath tempIndexDb(directory.getFilePath("project.srctrl.db.tmp"));
	writeIndex(indexDb, 1);
	writeIndex(tempIndexDb, 3);

	StorageConnection writer(tempIndexDb);
	SqliteIndexStorage storage(writer);
	storage.beginTransaction();
	storage.addNode(StorageNodeData(NODE_CLASS, "more"));
	storage.commitTransaction();

	const std::expected<void, IndexSwapError> replaced = replace(indexDb, tempIndexDb);

	REQUIRE_FALSE(replaced);
	REQUIRE(replaced.error() == IndexSwapError::TempIndexInUse);
	REQUIRE(nodeCount(indexDb) == 1);
}
