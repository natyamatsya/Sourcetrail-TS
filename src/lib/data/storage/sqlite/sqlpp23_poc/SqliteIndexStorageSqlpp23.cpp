#include "SqliteIndexStorageSqlpp23.h"

#include <sqlpp23/sqlite3/sqlite3.h>
#include <sqlpp23/sqlpp23.h>

#include "IndexTables.h"

namespace idx_poc {
namespace {
constexpr idx::Element element;
constexpr idx::Node node;
constexpr idx::Edge edge;
constexpr idx::SourceLocation sourceLocation;
constexpr idx::Occurrence occurrence;

// Aggregate result aliases (SQLPP_CREATE_NAME_TAG must live at namespace scope).
SQLPP_CREATE_NAME_TAG(maxId);
SQLPP_CREATE_NAME_TAG(cnt);

std::vector<std::int64_t> toI64(const std::vector<Id>& ids) {
  return {ids.begin(), ids.end()};
}
}  // namespace

void SqliteIndexStorageSqlpp23::setupTables() {
  // DDL through the same injected connection (unchanged from the raw layer).
  m_db("CREATE TABLE IF NOT EXISTS element(id INTEGER, PRIMARY KEY(id))");
  m_db(R"(CREATE TABLE IF NOT EXISTS node(
            id INTEGER NOT NULL, type INTEGER NOT NULL, serialized_name TEXT,
            PRIMARY KEY(id), FOREIGN KEY(id) REFERENCES element(id) ON DELETE CASCADE))");
  m_db(R"(CREATE TABLE IF NOT EXISTS edge(
            id INTEGER NOT NULL, type INTEGER NOT NULL,
            source_node_id INTEGER NOT NULL, target_node_id INTEGER NOT NULL,
            PRIMARY KEY(id),
            FOREIGN KEY(id) REFERENCES element(id) ON DELETE CASCADE,
            FOREIGN KEY(source_node_id) REFERENCES node(id) ON DELETE CASCADE,
            FOREIGN KEY(target_node_id) REFERENCES node(id) ON DELETE CASCADE))");
  m_db(R"(CREATE TABLE IF NOT EXISTS source_location(
            id INTEGER NOT NULL, file_node_id INTEGER,
            start_line INTEGER, start_column INTEGER, end_line INTEGER, end_column INTEGER,
            type INTEGER, PRIMARY KEY(id),
            FOREIGN KEY(file_node_id) REFERENCES node(id) ON DELETE CASCADE))");
  m_db(R"(CREATE TABLE IF NOT EXISTS occurrence(
            element_id INTEGER NOT NULL, source_location_id INTEGER NOT NULL,
            PRIMARY KEY(element_id, source_location_id),
            FOREIGN KEY(element_id) REFERENCES element(id) ON DELETE CASCADE,
            FOREIGN KEY(source_location_id) REFERENCES source_location(id) ON DELETE CASCADE))");
}

void SqliteIndexStorageSqlpp23::seedElementId() {
  using namespace sqlpp;
  m_nextElementId = 1;
  // MAX(id) is NULL on an empty table -> optional; fall back to 0 -> counter 1.
  for (const auto& row : m_db(select(max(element.id).as(maxId)).from(element))) {
    if (row.maxId) {
      m_nextElementId = static_cast<Id>(row.maxId.value()) + 1;
    }
  }
}

Id SqliteIndexStorageSqlpp23::insertElement() {
  using namespace sqlpp;
  if (m_nextElementId == 0) {
    seedElementId();
  }
  const Id id = m_nextElementId++;
  m_db(insert_into(element).set(element.id = static_cast<std::int64_t>(id)));
  return id;
}

std::vector<Id> SqliteIndexStorageSqlpp23::addNodes(const std::vector<StorageNodeData>& nodes) {
  using namespace sqlpp;
  std::vector<Id> ids;
  if (nodes.empty()) {
    return ids;
  }
  ids.reserve(nodes.size());

  // One multi-row INSERT for all nodes. This single statement replaces the entire
  // InsertBatchStatement<StorageNode> compile/execute machinery.
  auto insert = insert_into(node).columns(node.id, node.type, node.serializedName);
  for (const auto& n : nodes) {
    const Id id = insertElement();  // client-side id allocation
    ids.push_back(id);
    insert.add_values(
        node.id = static_cast<std::int64_t>(id),
        node.type = n.type,
        node.serializedName = n.serializedName);
  }
  m_db(insert);
  return ids;
}

std::vector<Id> SqliteIndexStorageSqlpp23::addEdges(const std::vector<StorageEdgeData>& edges) {
  using namespace sqlpp;
  std::vector<Id> ids;
  if (edges.empty()) {
    return ids;
  }
  ids.reserve(edges.size());

  auto insert = insert_into(edge).columns(edge.id, edge.type, edge.sourceNodeId, edge.targetNodeId);
  for (const auto& e : edges) {
    const Id id = insertElement();
    ids.push_back(id);
    insert.add_values(
        edge.id = static_cast<std::int64_t>(id),
        edge.type = e.type,
        edge.sourceNodeId = static_cast<std::int64_t>(e.sourceNodeId),
        edge.targetNodeId = static_cast<std::int64_t>(e.targetNodeId));
  }
  m_db(insert);
  return ids;
}

void SqliteIndexStorageSqlpp23::addSourceLocations(
    const std::vector<StorageSourceLocationData>& locations) {
  using namespace sqlpp;
  if (locations.empty()) {
    return;
  }
  // id omitted -> SQLite assigns rowids (source_location.id is a real auto PK).
  auto insert = insert_into(sourceLocation)
                    .columns(sourceLocation.fileNodeId,
                             sourceLocation.startLine,
                             sourceLocation.startColumn,
                             sourceLocation.endLine,
                             sourceLocation.endColumn,
                             sourceLocation.type);
  for (const auto& l : locations) {
    insert.add_values(
        sourceLocation.fileNodeId = static_cast<std::int64_t>(l.fileNodeId),
        sourceLocation.startLine = l.startLine,
        sourceLocation.startColumn = l.startCol,
        sourceLocation.endLine = l.endLine,
        sourceLocation.endColumn = l.endCol,
        sourceLocation.type = l.type);
  }
  m_db(insert);
}

void SqliteIndexStorageSqlpp23::addOccurrences(const std::vector<StorageOccurrence>& occurrences) {
  using namespace sqlpp;
  // INSERT OR IGNORE: the composite PK (element_id, source_location_id) makes
  // re-inserting an existing pair a silent no-op (matches the raw layer).
  for (const auto& o : occurrences) {
    m_db(sqlpp::sqlite3::insert_or_ignore().into(occurrence).set(
        occurrence.elementId = static_cast<std::int64_t>(o.elementId),
        occurrence.sourceLocationId = static_cast<std::int64_t>(o.sourceLocationId)));
  }
}

std::vector<StorageEdge> SqliteIndexStorageSqlpp23::getEdgesBySourceIds(
    const std::vector<Id>& sourceIds) const {
  using namespace sqlpp;
  std::vector<StorageEdge> result;
  if (sourceIds.empty()) {
    return result;
  }
  for (const auto& row :
       m_db(select(all_of(edge)).from(edge).where(edge.sourceNodeId.in(toI64(sourceIds))))) {
    result.push_back(StorageEdge{
        static_cast<Id>(row.id),
        static_cast<int>(row.type),
        static_cast<Id>(row.sourceNodeId),
        static_cast<Id>(row.targetNodeId)});
  }
  return result;
}

std::vector<StorageEdge> SqliteIndexStorageSqlpp23::getEdgesByTargetId(Id targetId) const {
  using namespace sqlpp;
  std::vector<StorageEdge> result;
  for (const auto& row : m_db(select(all_of(edge))
                                  .from(edge)
                                  .where(edge.targetNodeId == static_cast<std::int64_t>(targetId)))) {
    result.push_back(StorageEdge{
        static_cast<Id>(row.id),
        static_cast<int>(row.type),
        static_cast<Id>(row.sourceNodeId),
        static_cast<Id>(row.targetNodeId)});
  }
  return result;
}

std::vector<StorageSourceLocation> SqliteIndexStorageSqlpp23::getSourceLocationsForFile(
    Id fileNodeId) const {
  using namespace sqlpp;
  std::vector<StorageSourceLocation> result;
  for (const auto& row :
       m_db(select(all_of(sourceLocation))
                .from(sourceLocation)
                .where(sourceLocation.fileNodeId == static_cast<std::int64_t>(fileNodeId)))) {
    result.push_back(StorageSourceLocation{
        static_cast<Id>(row.id),
        static_cast<Id>(row.fileNodeId.value_or(0)),
        static_cast<int>(row.startLine.value_or(0)),
        static_cast<int>(row.startColumn.value_or(0)),
        static_cast<int>(row.endLine.value_or(0)),
        static_cast<int>(row.endColumn.value_or(0)),
        static_cast<int>(row.type.value_or(0))});
  }
  return result;
}

std::vector<StorageOccurrence> SqliteIndexStorageSqlpp23::getOccurrencesForElementIds(
    const std::vector<Id>& elementIds) const {
  using namespace sqlpp;
  std::vector<StorageOccurrence> result;
  if (elementIds.empty()) {
    return result;
  }
  for (const auto& row : m_db(select(all_of(occurrence))
                                  .from(occurrence)
                                  .where(occurrence.elementId.in(toI64(elementIds))))) {
    result.push_back(StorageOccurrence{
        static_cast<Id>(row.elementId), static_cast<Id>(row.sourceLocationId)});
  }
  return result;
}

int SqliteIndexStorageSqlpp23::getNodeCount() const {
  using namespace sqlpp;
  for (const auto& row : m_db(select(count(node.id).as(cnt)).from(node))) {
    return static_cast<int>(row.cnt);
  }
  return 0;
}

}  // namespace idx_poc
