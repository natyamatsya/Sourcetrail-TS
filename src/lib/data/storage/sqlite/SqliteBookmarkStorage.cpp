#include "SqliteBookmarkStorage.h"

#include <sqlpp23/sqlpp23.h>

#include "BookmarkTables.h"
#include "BorrowedSqliteConnection.h"
#include "logging.h"

namespace
{
// Table instances (stateless). Models generated from bookmark.sql; see
// BookmarkTables.h for the [PK-is-FK] manual corrections.
constexpr bm::BookmarkCategory categoryTable;
constexpr bm::Bookmark bookmarkTable;
constexpr bm::BookmarkedElement elementTable;
constexpr bm::BookmarkedNode nodeTable;
constexpr bm::BookmarkedEdge edgeTable;

// Read a nullable text column into std::string ("" when NULL) — matches the old
// getStringField(col, "") default.
template <typename Field>
std::string fieldText(const Field& field)
{
	return field.has_value() ? std::string(field.value()) : std::string();
}
}	 // namespace

const size_t SqliteBookmarkStorage::s_storageVersion = 2;

SqliteBookmarkStorage::SqliteBookmarkStorage(StorageConnection& connection): SqliteStorage(connection)
{
}

size_t SqliteBookmarkStorage::getStaticVersion() const
{
	return s_storageVersion;
}

StorageBookmarkCategory SqliteBookmarkStorage::addBookmarkCategory(const StorageBookmarkCategoryData& data)
{
	using namespace sqlpp;
	try
	{
		// id is omitted -> SQLite assigns the rowid; last_insert_id gives it back.
		const auto result = db()(insert_into(categoryTable).set(categoryTable.name = data.name));
		return StorageBookmarkCategory(static_cast<Id>(result.last_insert_id), data);
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return StorageBookmarkCategory();
}

StorageBookmark SqliteBookmarkStorage::addBookmark(const StorageBookmarkData& data)
{
	using namespace sqlpp;
	try
	{
		const auto result = db()(insert_into(bookmarkTable)
									 .set(bookmarkTable.name = data.name,
										  bookmarkTable.comment = data.comment,
										  bookmarkTable.timestamp = data.timestamp,
										  bookmarkTable.categoryId = static_cast<int64_t>(data.categoryId)));
		return StorageBookmark(BookmarkId(result.last_insert_id), data);
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return StorageBookmark();
}

StorageBookmarkedNode SqliteBookmarkStorage::addBookmarkedNode(const StorageBookmarkedNodeData& data)
{
	using namespace sqlpp;
	try
	{
		const auto elementResult = db()(insert_into(elementTable).set(
			elementTable.bookmarkId = static_cast<int64_t>(data.bookmarkId)));
		const Id id = static_cast<Id>(elementResult.last_insert_id);

		db()(insert_into(nodeTable).set(
			nodeTable.id = static_cast<int64_t>(id),
			nodeTable.serializedNodeName = data.serializedNodeName));

		return StorageBookmarkedNode(id, data);
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return StorageBookmarkedNode();
}

StorageBookmarkedEdge SqliteBookmarkStorage::addBookmarkedEdge(const StorageBookmarkedEdgeData data)
{
	using namespace sqlpp;
	try
	{
		const auto elementResult = db()(insert_into(elementTable).set(
			elementTable.bookmarkId = static_cast<int64_t>(data.bookmarkId)));
		const Id id = static_cast<Id>(elementResult.last_insert_id);

		db()(insert_into(edgeTable).set(
			edgeTable.id = static_cast<int64_t>(id),
			edgeTable.serializedSourceNodeName = data.serializedSourceNodeName,
			edgeTable.serializedTargetNodeName = data.serializedTargetNodeName,
			edgeTable.edgeType = data.edgeType,
			edgeTable.sourceNodeActive = data.sourceNodeActive ? 1 : 0));

		return StorageBookmarkedEdge(id, data);
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return StorageBookmarkedEdge();
}

std::vector<StorageBookmark> SqliteBookmarkStorage::getAllBookmarks() const
{
	using namespace sqlpp;
	std::vector<StorageBookmark> bookmarks;
	try
	{
		for (const auto& row: db()(select(all_of(bookmarkTable)).from(bookmarkTable)))
		{
			const auto id = BookmarkId(row.id);
			std::string name = fieldText(row.name);
			std::string timestamp = fieldText(row.timestamp);

			if (id != BookmarkId::NONE && !name.empty() && !timestamp.empty())
			{
				bookmarks.push_back(StorageBookmark(
					id,
					std::move(name),
					fieldText(row.comment),
					std::move(timestamp),
					static_cast<Id>(row.categoryId.value_or(0))));
			}
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return bookmarks;
}

void SqliteBookmarkStorage::removeBookmark(const BookmarkId bookmarkId)
{
	using namespace sqlpp;
	try
	{
		db()(delete_from(bookmarkTable).where(bookmarkTable.id == static_cast<int64_t>(bookmarkId)));
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
}

std::vector<StorageBookmarkedNode> SqliteBookmarkStorage::getAllBookmarkedNodes() const
{
	using namespace sqlpp;
	std::vector<StorageBookmarkedNode> bookmarkedNodes;
	try
	{
		for (const auto& row: db()(select(nodeTable.id, elementTable.bookmarkId, nodeTable.serializedNodeName)
									   .from(nodeTable.join(elementTable).on(nodeTable.id == elementTable.id))))
		{
			const Id id = static_cast<Id>(row.id);
			const auto bookmarkId = BookmarkId(row.bookmarkId);
			std::string serializedNodeName = fieldText(row.serializedNodeName);

			if (id != 0 && bookmarkId != BookmarkId::NONE && !serializedNodeName.empty())
			{
				bookmarkedNodes.push_back(StorageBookmarkedNode(id, bookmarkId, std::move(serializedNodeName)));
			}
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return bookmarkedNodes;
}

std::vector<StorageBookmarkedEdge> SqliteBookmarkStorage::getAllBookmarkedEdges() const
{
	using namespace sqlpp;
	std::vector<StorageBookmarkedEdge> bookmarkedEdges;
	try
	{
		for (const auto& row: db()(select(edgeTable.id,
										  elementTable.bookmarkId,
										  edgeTable.serializedSourceNodeName,
										  edgeTable.serializedTargetNodeName,
										  edgeTable.edgeType,
										  edgeTable.sourceNodeActive)
									   .from(edgeTable.join(elementTable).on(edgeTable.id == elementTable.id))))
		{
			const Id id = static_cast<Id>(row.id);
			const auto bookmarkId = BookmarkId(row.bookmarkId);
			std::string serializedSourceNodeName = fieldText(row.serializedSourceNodeName);
			std::string serializedTargetNodeName = fieldText(row.serializedTargetNodeName);
			const int edgeType = row.edgeType.has_value() ? static_cast<int>(row.edgeType.value()) : -1;
			const int sourceNodeActive =
				row.sourceNodeActive.has_value() ? static_cast<int>(row.sourceNodeActive.value()) : -1;

			if (id != 0 && bookmarkId != BookmarkId::NONE && !serializedSourceNodeName.empty() &&
				!serializedTargetNodeName.empty() && edgeType != -1 && sourceNodeActive != -1)
			{
				bookmarkedEdges.push_back(StorageBookmarkedEdge(
					id,
					bookmarkId,
					std::move(serializedSourceNodeName),
					std::move(serializedTargetNodeName),
					edgeType,
					sourceNodeActive != 0));
			}
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return bookmarkedEdges;
}

void SqliteBookmarkStorage::updateBookmark(
	const BookmarkId bookmarkId, const std::string& name, const std::string& comment, const Id categoryId)
{
	using namespace sqlpp;
	// The raw path issued three separate UPDATEs built by string concatenation
	// (an injection hazard on name/comment). One typed, escaped statement
	// replaces all three.
	try
	{
		db()(update(bookmarkTable)
				 .set(bookmarkTable.name = name,
					  bookmarkTable.comment = comment,
					  bookmarkTable.categoryId = static_cast<int64_t>(categoryId))
				 .where(bookmarkTable.id == static_cast<int64_t>(bookmarkId)));
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
}

std::vector<StorageBookmarkCategory> SqliteBookmarkStorage::getAllBookmarkCategories() const
{
	using namespace sqlpp;
	std::vector<StorageBookmarkCategory> categories;
	try
	{
		for (const auto& row: db()(select(all_of(categoryTable)).from(categoryTable)))
		{
			const Id id = static_cast<Id>(row.id);
			std::string name = fieldText(row.name);

			if (id != 0 && !name.empty())
			{
				categories.push_back(StorageBookmarkCategory(id, std::move(name)));
			}
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return categories;
}

StorageBookmarkCategory SqliteBookmarkStorage::getBookmarkCategoryByName(const std::string& name) const
{
	using namespace sqlpp;
	try
	{
		for (const auto& row:
			 db()(select(all_of(categoryTable)).from(categoryTable).where(categoryTable.name == name)))
		{
			return StorageBookmarkCategory(static_cast<Id>(row.id), fieldText(row.name));
		}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
	return StorageBookmarkCategory();
}

void SqliteBookmarkStorage::removeBookmarkCategory(Id id)
{
	using namespace sqlpp;
	try
	{
		db()(delete_from(categoryTable).where(categoryTable.id == static_cast<int64_t>(id)));
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(e.what());
	}
}

std::vector<std::pair<int, SqliteDatabaseIndex>> SqliteBookmarkStorage::getIndices() const
{
	return std::vector<std::pair<int, SqliteDatabaseIndex>>();
}

void SqliteBookmarkStorage::clearTables()
{
	// Schema DDL is not yet expressed in the type-safe DSL; it stays on the raw
	// view of the shared handle.
	try
	{
		m_database.execDML("DROP TABLE IF EXISTS main.bookmarked_edge;");
		m_database.execDML("DROP TABLE IF EXISTS main.bookmarked_node;");
		m_database.execDML("DROP TABLE IF EXISTS main.bookmarked_element;");
		m_database.execDML("DROP TABLE IF EXISTS main.bookmark;");
		m_database.execDML("DROP TABLE IF EXISTS main.bookmark_category;");
	}
	catch (CppSQLite3Exception& e)
	{
		LOG_ERROR(std::to_string(e.errorCode()) + ": " + e.errorMessage());
	}
}

void SqliteBookmarkStorage::setupTables()
{
	// Keep this DDL in sync with bookmark.sql (the source for BookmarkTables.h).
	try
	{
		m_database.execDML(
			"CREATE TABLE IF NOT EXISTS bookmark_category("
			"id INTEGER NOT NULL, "
			"name TEXT, "
			"PRIMARY KEY(id)"
			");");

		m_database.execDML(
			"CREATE TABLE IF NOT EXISTS bookmark("
			"id INTEGER NOT NULL, "
			"name TEXT, "
			"comment TEXT, "
			"timestamp TEXT, "
			"category_id INTEGER, "
			"FOREIGN KEY(category_id) REFERENCES bookmark_category(id) ON DELETE CASCADE, "
			"PRIMARY KEY(id)"
			");");

		m_database.execDML(
			"CREATE TABLE IF NOT EXISTS bookmarked_element("
			"id INTEGER NOT NULL, "
			"bookmark_id INTEGER NOT NULL, "
			"FOREIGN KEY(bookmark_id) REFERENCES bookmark(id) ON DELETE CASCADE, "
			"PRIMARY KEY(id)"
			");");

		m_database.execDML(
			"CREATE TABLE IF NOT EXISTS bookmarked_node("
			"id INTEGER NOT NULL, "
			"serialized_node_name TEXT, "
			"FOREIGN KEY(id) REFERENCES bookmarked_element(id) ON DELETE CASCADE, "
			"PRIMARY KEY(id)"
			");");

		m_database.execDML(
			"CREATE TABLE IF NOT EXISTS bookmarked_edge("
			"id INTEGER NOT NULL, "
			"serialized_source_node_name TEXT, "
			"serialized_target_node_name TEXT, "
			"edge_type INTEGER, "
			"source_node_active INTEGER, "
			"FOREIGN KEY(id) REFERENCES bookmarked_element(id) ON DELETE CASCADE, "
			"PRIMARY KEY(id)"
			");");
	}
	catch (CppSQLite3Exception& e)
	{
		LOG_ERROR_STREAM(
			<< "Failed to create tables: " << std::to_string(e.errorCode()) << ": "
			<< e.errorMessage());
		throw e;
	}
	catch (std::exception& e)
	{
		LOG_ERROR_STREAM(<< "Failed to create tables: " << e.what());
		throw e;
	}
}

void SqliteBookmarkStorage::setupPrecompiledStatements() {}
