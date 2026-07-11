#pragma once

// Focused proof-of-concept: the NOVEL parts of SqliteIndexStorage on sqlpp23.
//
// The bookmark POC already proved typed CRUD + IoC injection + the raw-SQL
// escape hatch. This one targets what the index storage adds on top and what was
// the biggest open question in the migration analysis:
//
//   1. Batch multi-row INSERT  -> replaces the hand-rolled InsertBatchStatement<T>
//      and its 999-bound-parameter dance. sqlpp23 builds one INSERT with N value
//      tuples via .columns(...) + repeated .add_values(...); direct execution
//      inlines the values (properly escaped), so the SQLITE_MAX_VARIABLE_NUMBER
//      limit that forced the dance does not apply. (For very large batches, chunk
//      by statement length, not by a hard 999.)
//   2. Client-side id allocation -> insertElement() mints ids from an in-process
//      counter seeded from MAX(id), reproducing SQLite's autoincrement without an
//      INSERT+lastRowId round-trip (the SOURCETRAIL_CLIENT_IDS write path).
//   3. Representative reads      -> IN(id-list) filters and an aggregate count().
//
// Same IoC shape as the bookmark POC: the connection is injected, not owned.
// Storage* structs are minimal stand-ins for the project types (field names 1:1).
// Dedup temp-indices (m_tempNodeNameIndex etc.) are orthogonal in-memory logic and
// unchanged by the SQL backend, so they are intentionally omitted here.

#include <cstdint>
#include <string>
#include <vector>

#include <sqlpp23/sqlite3/database/connection.h>

namespace idx_poc {

using Id = std::uint32_t;

struct StorageNodeData {
  int type = 0;
  std::string serializedName;
};
struct StorageNode {
  Id id = 0;
  int type = 0;
  std::string serializedName;
};

struct StorageEdgeData {
  int type = 0;
  Id sourceNodeId = 0;
  Id targetNodeId = 0;
};
struct StorageEdge {
  Id id = 0;
  int type = 0;
  Id sourceNodeId = 0;
  Id targetNodeId = 0;
};

struct StorageSourceLocationData {
  Id fileNodeId = 0;
  int startLine = 0;
  int startCol = 0;
  int endLine = 0;
  int endCol = 0;
  int type = 0;
};
struct StorageSourceLocation {
  Id id = 0;
  Id fileNodeId = 0;
  int startLine = 0;
  int startCol = 0;
  int endLine = 0;
  int endCol = 0;
  int type = 0;
};

struct StorageOccurrence {
  Id elementId = 0;
  Id sourceLocationId = 0;
};

class SqliteIndexStorageSqlpp23 {
 public:
  explicit SqliteIndexStorageSqlpp23(sqlpp::sqlite3::connection& db) : m_db(db) {}

  void setupTables();

  // Batch inserts (the headline: one statement, no InsertBatchStatement machinery).
  std::vector<Id> addNodes(const std::vector<StorageNodeData>& nodes);
  std::vector<Id> addEdges(const std::vector<StorageEdgeData>& edges);
  void addSourceLocations(const std::vector<StorageSourceLocationData>& locations);
  void addOccurrences(const std::vector<StorageOccurrence>& occurrences);  // INSERT OR IGNORE

  // Reads.
  std::vector<StorageEdge> getEdgesBySourceIds(const std::vector<Id>& sourceIds) const;
  std::vector<StorageEdge> getEdgesByTargetId(Id targetId) const;
  std::vector<StorageSourceLocation> getSourceLocationsForFile(Id fileNodeId) const;
  std::vector<StorageOccurrence> getOccurrencesForElementIds(const std::vector<Id>& elementIds) const;

  int getNodeCount() const;

 private:
  // Client-side id allocation (SOURCETRAIL_CLIENT_IDS path): mint an element id
  // from an in-process counter seeded lazily from MAX(id).
  Id insertElement();
  void seedElementId();

  sqlpp::sqlite3::connection& m_db;
  Id m_nextElementId = 0;  // 0 = unseeded
};

}  // namespace idx_poc
