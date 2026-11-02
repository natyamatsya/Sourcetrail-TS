#include "Catch2.hpp"

#ifndef SRCTRL_MODULE_BUILD
#include "FileSystem.h"
#endif
#include "SqliteBookmarkStorage.h"
#include "StorageConnection.h"
#include "TestTempDirectory.h"

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.file;
#endif

TEST_CASE("add bookmarks")
{
	const TestTempDirectory tempDirectory{"SqliteBookmarkStorageTestSuite"};
	const FilePath databasePath(tempDirectory.getFilePath("bookmarkTest.sqlite"));
	int bookmarkCount = 4;
	int result = -1;
	{
		FileSystem::remove(databasePath);
		StorageConnection connection(databasePath);
		SqliteBookmarkStorage storage(connection);
		storage.setup();

		for (int i = 0; i < bookmarkCount; i++)
		{
			const Id categoryId =
				storage.addBookmarkCategory(StorageBookmarkCategoryData("test category")).id;
			storage.addBookmark(StorageBookmarkData(
				"test bookmark", "test comment", TimeStamp::now().toString(), categoryId));
		}

		result = static_cast<int>(storage.getAllBookmarks().size());
	}

	FileSystem::remove(databasePath);

	REQUIRE(result == bookmarkCount);
}

TEST_CASE("add bookmarked node")
{
	const TestTempDirectory tempDirectory{"SqliteBookmarkStorageTestSuite"};
	const FilePath databasePath(tempDirectory.getFilePath("bookmarkTest.sqlite"));
	int bookmarkCount = 4;
	int result = -1;
	{
		FileSystem::remove(databasePath);
		StorageConnection connection(databasePath);
		SqliteBookmarkStorage storage(connection);
		storage.setup();

		const Id categoryId = storage.addBookmarkCategory(StorageBookmarkCategoryData("test category")).id;
		const BookmarkId bookmarkId = storage.addBookmark(StorageBookmarkData("test bookmark", "test comment", TimeStamp::now().toString(), categoryId)).bookmarkId;

		for (int i = 0; i < bookmarkCount; i++)
		{
			storage.addBookmarkedNode(StorageBookmarkedNodeData(bookmarkId, "test name"));
		}

		result = static_cast<int>(storage.getAllBookmarkedNodes().size());
	}

	FileSystem::remove(databasePath);

	REQUIRE(result == bookmarkCount);
}

TEST_CASE("remove bookmark also removes bookmarked node")
{
	const TestTempDirectory tempDirectory{"SqliteBookmarkStorageTestSuite"};
	const FilePath databasePath(tempDirectory.getFilePath("bookmarkTest.sqlite"));
	int result = -1;
	{
		FileSystem::remove(databasePath);
		StorageConnection connection(databasePath);
		SqliteBookmarkStorage storage(connection);
		storage.setup();

		const Id categoryId = storage.addBookmarkCategory(StorageBookmarkCategoryData("test category")).id;
		const BookmarkId bookmarkId = storage.addBookmark(StorageBookmarkData( "test bookmark", "test comment", TimeStamp::now().toString(), categoryId)).bookmarkId;
		storage.addBookmarkedNode(StorageBookmarkedNodeData(bookmarkId, "test name"));

		storage.removeBookmark(bookmarkId);

		result = static_cast<int>(storage.getAllBookmarkedNodes().size());
	}

	FileSystem::remove(databasePath);

	REQUIRE(result == 0);
}

TEST_CASE("edit nodeBookmark")
{
	const TestTempDirectory tempDirectory{"SqliteBookmarkStorageTestSuite"};
	const FilePath databasePath(tempDirectory.getFilePath("bookmarkTest.sqlite"));

	const std::string updatedName = "updated name";
	const std::string updatedComment = "updated comment";

	StorageBookmark storageBookmark;
	{
		FileSystem::remove(databasePath);
		StorageConnection connection(databasePath);
		SqliteBookmarkStorage storage(connection);
		storage.setup();

		const Id categoryId = storage.addBookmarkCategory(StorageBookmarkCategoryData("test category")).id;
		const BookmarkId bookmarkId = storage.addBookmark(StorageBookmarkData("test bookmark", "test comment", TimeStamp::now().toString(), categoryId)).bookmarkId;
		storage.addBookmarkedNode(StorageBookmarkedNodeData(bookmarkId, "test name"));

		storage.updateBookmark(bookmarkId, updatedName, updatedComment, categoryId);

		storageBookmark = storage.getAllBookmarks().front();
	}

	FileSystem::remove(databasePath);

	REQUIRE(updatedName == storageBookmark.name);
	REQUIRE(updatedComment == storageBookmark.comment);
}

TEST_CASE("bookmark storage keeps the tabs' session")
{
	const TestTempDirectory tempDirectory{"SqliteBookmarkStorageTestSuite"};
	const FilePath databasePath(tempDirectory.getFilePath("sessionTest.sqlite"));
	FileSystem::remove(databasePath);
	{
		StorageConnection connection(databasePath);
		SqliteBookmarkStorage storage(connection);
		storage.setup();
		REQUIRE(storage.getSession().empty());
		storage.setSession(R"({"version":1})");
		storage.setSession(R"({"version":1,"tabs":[]})");
	}
	{
		StorageConnection connection(databasePath);
		SqliteBookmarkStorage storage(connection);
		storage.setup();
		REQUIRE(storage.getSession() == R"({"version":1,"tabs":[]})");
	}
	FileSystem::remove(databasePath);
}

// A NUL in a bookmark's name or category cut the SQL short ("unrecognized token"): the
// bookmark was not saved, while the app said it was created.
TEST_CASE("bookmark text with a NUL round-trips")
{
	using namespace std::string_literals;
	const TestTempDirectory tempDirectory{"SqliteBookmarkStorageTestSuite"};
	const FilePath databasePath(tempDirectory.getFilePath("bookmarkTest.sqlite"));
	StorageConnection connection(databasePath);
	SqliteBookmarkStorage storage(connection);
	storage.setup();
	const std::string category = "cat\0egory"s;
	const std::string name = "x\0y 'quoted'"s;
	const std::string comment = "a\0comment"s;
	const std::string nodeName = "node\0name"s;

	const Id categoryId = storage.addBookmarkCategory(StorageBookmarkCategoryData(category)).id;
	REQUIRE(categoryId != 0);
	REQUIRE(storage.getBookmarkCategoryByName(category).id == categoryId);
	const BookmarkId bookmarkId =
		storage.addBookmark(StorageBookmarkData(name, comment, TimeStamp::now().toString(), categoryId)).bookmarkId;
	REQUIRE(bookmarkId != BookmarkId::NONE);
	REQUIRE(storage.addBookmarkedNode(StorageBookmarkedNodeData(bookmarkId, nodeName)).id != 0);

	const std::vector<StorageBookmark> bookmarks = storage.getAllBookmarks();
	REQUIRE(bookmarks.size() == 1);
	REQUIRE(bookmarks.front().name == name);
	REQUIRE(bookmarks.front().comment == comment);
	REQUIRE(storage.getAllBookmarkCategories().front().name == category);
	REQUIRE(storage.getAllBookmarkedNodes().front().serializedNodeName == nodeName);

	storage.updateBookmark(bookmarkId, name + "\0more"s, comment, categoryId);
	REQUIRE(storage.getAllBookmarks().front().name == name + "\0more"s);
}
