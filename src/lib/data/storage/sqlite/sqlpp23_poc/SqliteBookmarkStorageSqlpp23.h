#pragma once

// Proof-of-concept: SqliteBookmarkStorage reimplemented on sqlpp23.
//
// Key design decision (inversion of control):
//   The connection is NOT created here. It is injected through the constructor
//   as `sqlpp::sqlite3::connection&`. Whoever owns the bookmark database owns
//   the one connection; this class only borrows it. That removes the
//   "each layer opens its own handle" problem: schema/meta/pragmas and the
//   typed bookmark CRUD all run on the SAME connection, so they share one
//   transaction scope. See README.md for how this generalizes to SqliteStorage.
//
// The Storage* / *Data structs below are minimal stand-ins for the real project
// types (StorageBookmark, StorageBookmarkData, ...) so the POC builds without
// pulling in the whole codebase. Field names match 1:1, so swapping in the real
// headers is mechanical.

#include <cstdint>
#include <string>
#include <vector>

#include <sqlpp23/sqlite3/database/connection.h>

namespace bm_poc {

using Id = std::uint32_t;          // stands in for the project's `Id`
using BookmarkId = std::uint32_t;  // stands in for the project's strong `BookmarkId`

struct StorageBookmarkCategory {
  Id id = 0;
  std::string name;
};

struct StorageBookmark {
  BookmarkId id = 0;
  std::string name;
  std::string comment;
  std::string timestamp;
  Id categoryId = 0;
};

struct StorageBookmarkedNode {
  Id id = 0;
  BookmarkId bookmarkId = 0;
  std::string serializedNodeName;
};

struct StorageBookmarkedEdge {
  Id id = 0;
  BookmarkId bookmarkId = 0;
  std::string serializedSourceNodeName;
  std::string serializedTargetNodeName;
  int edgeType = 0;
  bool sourceNodeActive = false;
};

// "Data" inputs (no id yet), mirroring StorageBookmark*Data.
struct StorageBookmarkCategoryData {
  std::string name;
};
struct StorageBookmarkData {
  std::string name;
  std::string comment;
  std::string timestamp;
  Id categoryId = 0;
};
struct StorageBookmarkedNodeData {
  BookmarkId bookmarkId = 0;
  std::string serializedNodeName;
};
struct StorageBookmarkedEdgeData {
  BookmarkId bookmarkId = 0;
  std::string serializedSourceNodeName;
  std::string serializedTargetNodeName;
  int edgeType = 0;
  bool sourceNodeActive = false;
};

class SqliteBookmarkStorageSqlpp23 {
 public:
  // Inversion of control: the connection is injected, not owned.
  explicit SqliteBookmarkStorageSqlpp23(sqlpp::sqlite3::connection& db) : m_db(db) {}

  void setupTables();
  void clearTables();

  StorageBookmarkCategory addBookmarkCategory(const StorageBookmarkCategoryData& data);
  StorageBookmark addBookmark(const StorageBookmarkData& data);
  StorageBookmarkedNode addBookmarkedNode(const StorageBookmarkedNodeData& data);
  StorageBookmarkedEdge addBookmarkedEdge(const StorageBookmarkedEdgeData& data);

  void removeBookmarkCategory(Id id);
  void removeBookmark(BookmarkId bookmarkId);

  void updateBookmark(
      BookmarkId bookmarkId, const std::string& name, const std::string& comment, Id categoryId);

  std::vector<StorageBookmarkCategory> getAllBookmarkCategories() const;
  StorageBookmarkCategory getBookmarkCategoryByName(const std::string& name) const;

  std::vector<StorageBookmark> getAllBookmarks() const;
  std::vector<StorageBookmarkedNode> getAllBookmarkedNodes() const;
  std::vector<StorageBookmarkedEdge> getAllBookmarkedEdges() const;

 private:
  sqlpp::sqlite3::connection& m_db;
};

}  // namespace bm_poc
