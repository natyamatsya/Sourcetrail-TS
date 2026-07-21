#include "Catch2.hpp"

#ifndef SRCTRL_MODULE_BUILD
#include "FileSystem.h"
#endif
#include "SqliteBookmarkStorage.h"
#include "StorageConnection.h"

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.file;
#endif

TEST_CASE("add bookmarks")
{
	FilePath databasePath("data/SQLiteTestSuite/bookmarkTest.sqlite");
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
	FilePath databasePath("data/SQLiteTestSuite/bookmarkTest.sqlite");
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
	FilePath databasePath("data/SQLiteTestSuite/bookmarkTest.sqlite");
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
	FilePath databasePath("data/SQLiteTestSuite/bookmarkTest.sqlite");

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
