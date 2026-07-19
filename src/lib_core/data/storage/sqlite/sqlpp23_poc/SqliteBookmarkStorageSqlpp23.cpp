#include "SqliteBookmarkStorageSqlpp23.h"

#include <sqlpp23/sqlpp23.h>

#include "BookmarkTables.h"

namespace bm_poc {
namespace {
// Table instances (stateless).
constexpr bm::BookmarkCategory category;
constexpr bm::Bookmark bookmark;
constexpr bm::BookmarkedElement element;
constexpr bm::BookmarkedNode node;
constexpr bm::BookmarkedEdge edge;

// Read a nullable text column into a std::string ("" when NULL) — matches the
// old getStringField(col, "") default behavior.
template <typename Field>
std::string text(const Field& f) {
  return f.has_value() ? std::string(f.value()) : std::string();
}
}  // namespace

void SqliteBookmarkStorageSqlpp23::setupTables() {
  using namespace sqlpp;
  // Schema DDL is not yet expressed in the type-safe DSL, so it runs as raw SQL
  // *through the same injected connection*. This is the unified single-handle
  // path: un-migrated statements and typed queries share one connection.
  m_db(R"(CREATE TABLE IF NOT EXISTS bookmark_category(
            id INTEGER NOT NULL, name TEXT, PRIMARY KEY(id)))");
  m_db(R"(CREATE TABLE IF NOT EXISTS bookmark(
            id INTEGER NOT NULL, name TEXT, comment TEXT, timestamp TEXT, category_id INTEGER,
            FOREIGN KEY(category_id) REFERENCES bookmark_category(id) ON DELETE CASCADE,
            PRIMARY KEY(id)))");
  m_db(R"(CREATE TABLE IF NOT EXISTS bookmarked_element(
            id INTEGER NOT NULL, bookmark_id INTEGER NOT NULL,
            FOREIGN KEY(bookmark_id) REFERENCES bookmark(id) ON DELETE CASCADE,
            PRIMARY KEY(id)))");
  m_db(R"(CREATE TABLE IF NOT EXISTS bookmarked_node(
            id INTEGER NOT NULL, serialized_node_name TEXT,
            FOREIGN KEY(id) REFERENCES bookmarked_element(id) ON DELETE CASCADE,
            PRIMARY KEY(id)))");
  m_db(R"(CREATE TABLE IF NOT EXISTS bookmarked_edge(
            id INTEGER NOT NULL, serialized_source_node_name TEXT, serialized_target_node_name TEXT,
            edge_type INTEGER, source_node_active INTEGER,
            FOREIGN KEY(id) REFERENCES bookmarked_element(id) ON DELETE CASCADE,
            PRIMARY KEY(id)))");
}

void SqliteBookmarkStorageSqlpp23::clearTables() {
  m_db("DROP TABLE IF EXISTS bookmarked_edge");
  m_db("DROP TABLE IF EXISTS bookmarked_node");
  m_db("DROP TABLE IF EXISTS bookmarked_element");
  m_db("DROP TABLE IF EXISTS bookmark");
  m_db("DROP TABLE IF EXISTS bookmark_category");
}

StorageBookmarkCategory SqliteBookmarkStorageSqlpp23::addBookmarkCategory(
    const StorageBookmarkCategoryData& data) {
  using namespace sqlpp;
  // id is omitted -> SQLite assigns the rowid; last_insert_id gives it back.
  const auto result = m_db(insert_into(category).set(category.name = data.name));
  return StorageBookmarkCategory{static_cast<Id>(result.last_insert_id), data.name};
}

StorageBookmark SqliteBookmarkStorageSqlpp23::addBookmark(const StorageBookmarkData& data) {
  using namespace sqlpp;
  const auto result = m_db(insert_into(bookmark).set(
      bookmark.name = data.name,
      bookmark.comment = data.comment,
      bookmark.timestamp = data.timestamp,
      bookmark.categoryId = static_cast<int64_t>(data.categoryId)));
  return StorageBookmark{
      static_cast<BookmarkId>(result.last_insert_id),
      data.name,
      data.comment,
      data.timestamp,
      data.categoryId};
}

StorageBookmarkedNode SqliteBookmarkStorageSqlpp23::addBookmarkedNode(
    const StorageBookmarkedNodeData& data) {
  using namespace sqlpp;
  const auto elementResult =
      m_db(insert_into(element).set(element.bookmarkId = static_cast<int64_t>(data.bookmarkId)));
  const Id id = static_cast<Id>(elementResult.last_insert_id);

  m_db(insert_into(node).set(node.id = id, node.serializedNodeName = data.serializedNodeName));
  return StorageBookmarkedNode{id, data.bookmarkId, data.serializedNodeName};
}

StorageBookmarkedEdge SqliteBookmarkStorageSqlpp23::addBookmarkedEdge(
    const StorageBookmarkedEdgeData& data) {
  using namespace sqlpp;
  const auto elementResult =
      m_db(insert_into(element).set(element.bookmarkId = static_cast<int64_t>(data.bookmarkId)));
  const Id id = static_cast<Id>(elementResult.last_insert_id);

  m_db(insert_into(edge).set(
      edge.id = id,
      edge.serializedSourceNodeName = data.serializedSourceNodeName,
      edge.serializedTargetNodeName = data.serializedTargetNodeName,
      edge.edgeType = data.edgeType,
      edge.sourceNodeActive = data.sourceNodeActive ? 1 : 0));
  return StorageBookmarkedEdge{
      id,
      data.bookmarkId,
      data.serializedSourceNodeName,
      data.serializedTargetNodeName,
      data.edgeType,
      data.sourceNodeActive};
}

void SqliteBookmarkStorageSqlpp23::removeBookmarkCategory(Id id) {
  using namespace sqlpp;
  m_db(delete_from(category).where(category.id == static_cast<int64_t>(id)));
}

void SqliteBookmarkStorageSqlpp23::removeBookmark(BookmarkId bookmarkId) {
  using namespace sqlpp;
  m_db(delete_from(bookmark).where(bookmark.id == static_cast<int64_t>(bookmarkId)));
}

void SqliteBookmarkStorageSqlpp23::updateBookmark(
    BookmarkId bookmarkId, const std::string& name, const std::string& comment, Id categoryId) {
  using namespace sqlpp;
  // Original code issued three separate UPDATE statements built by string
  // concatenation (an injection risk on `name`/`comment`). One typed, bound
  // statement replaces all three.
  m_db(update(bookmark)
           .set(bookmark.name = name,
                bookmark.comment = comment,
                bookmark.categoryId = static_cast<int64_t>(categoryId))
           .where(bookmark.id == static_cast<int64_t>(bookmarkId)));
}

std::vector<StorageBookmarkCategory> SqliteBookmarkStorageSqlpp23::getAllBookmarkCategories() const {
  using namespace sqlpp;
  std::vector<StorageBookmarkCategory> categories;
  for (const auto& row : m_db(select(all_of(category)).from(category))) {
    const Id id = static_cast<Id>(row.id);
    std::string name = text(row.name);
    if (id != 0 && !name.empty()) {
      categories.push_back(StorageBookmarkCategory{id, std::move(name)});
    }
  }
  return categories;
}

StorageBookmarkCategory SqliteBookmarkStorageSqlpp23::getBookmarkCategoryByName(
    const std::string& name) const {
  using namespace sqlpp;
  for (const auto& row :
       m_db(select(all_of(category)).from(category).where(category.name == name))) {
    return StorageBookmarkCategory{static_cast<Id>(row.id), text(row.name)};
  }
  return StorageBookmarkCategory{};
}

std::vector<StorageBookmark> SqliteBookmarkStorageSqlpp23::getAllBookmarks() const {
  using namespace sqlpp;
  std::vector<StorageBookmark> bookmarks;
  for (const auto& row : m_db(select(all_of(bookmark)).from(bookmark))) {
    const auto id = static_cast<BookmarkId>(row.id);
    std::string name = text(row.name);
    std::string timestamp = text(row.timestamp);
    if (id != 0 && !name.empty() && !timestamp.empty()) {
      bookmarks.push_back(StorageBookmark{
          id,
          std::move(name),
          text(row.comment),
          std::move(timestamp),
          static_cast<Id>(row.categoryId.value_or(0))});
    }
  }
  return bookmarks;
}

std::vector<StorageBookmarkedNode> SqliteBookmarkStorageSqlpp23::getAllBookmarkedNodes() const {
  using namespace sqlpp;
  std::vector<StorageBookmarkedNode> nodes;
  for (const auto& row : m_db(select(node.id, element.bookmarkId, node.serializedNodeName)
                                  .from(node.join(element).on(node.id == element.id)))) {
    const Id id = static_cast<Id>(row.id);
    const auto bookmarkId = static_cast<BookmarkId>(row.bookmarkId);
    std::string serializedNodeName = text(row.serializedNodeName);
    if (id != 0 && bookmarkId != 0 && !serializedNodeName.empty()) {
      nodes.push_back(StorageBookmarkedNode{id, bookmarkId, std::move(serializedNodeName)});
    }
  }
  return nodes;
}

std::vector<StorageBookmarkedEdge> SqliteBookmarkStorageSqlpp23::getAllBookmarkedEdges() const {
  using namespace sqlpp;
  std::vector<StorageBookmarkedEdge> edges;
  for (const auto& row : m_db(select(edge.id,
                                     element.bookmarkId,
                                     edge.serializedSourceNodeName,
                                     edge.serializedTargetNodeName,
                                     edge.edgeType,
                                     edge.sourceNodeActive)
                                  .from(edge.join(element).on(edge.id == element.id)))) {
    const Id id = static_cast<Id>(row.id);
    const auto bookmarkId = static_cast<BookmarkId>(row.bookmarkId);
    std::string source = text(row.serializedSourceNodeName);
    std::string target = text(row.serializedTargetNodeName);
    const int edgeType = row.edgeType ? static_cast<int>(*row.edgeType) : -1;
    const int sourceNodeActive = row.sourceNodeActive ? static_cast<int>(*row.sourceNodeActive) : -1;
    if (id != 0 && bookmarkId != 0 && !source.empty() && !target.empty() && edgeType != -1 &&
        sourceNodeActive != -1) {
      edges.push_back(StorageBookmarkedEdge{
          id, bookmarkId, std::move(source), std::move(target), edgeType, sourceNodeActive != 0});
    }
  }
  return edges;
}

}  // namespace bm_poc
