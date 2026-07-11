// Driver proving the focused SqliteIndexStorage conversion compiles and runs:
// batch multi-row inserts, client-side id allocation, IN(id-list) reads, an
// aggregate count, and INSERT OR IGNORE dedup.

#include <cassert>
#include <iostream>
#include <memory>

#include <sqlpp23/sqlite3/database/connection.h>
#include <sqlpp23/sqlite3/database/connection_config.h>

#include "SqliteIndexStorageSqlpp23.h"

using namespace idx_poc;

int main() {
  auto config = std::make_shared<sqlpp::sqlite3::connection_config>();
  config->path_to_database = ":memory:";
  config->flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
  sqlpp::sqlite3::connection db{config};
  db("PRAGMA foreign_keys=ON");

  SqliteIndexStorageSqlpp23 storage{db};  // IoC: injected connection
  storage.setupTables();

  // ---- batch node insert + client-side id allocation -----------------------
  const auto nodeIds = storage.addNodes({
      {1, "a::Foo"},
      {1, "a::Bar"},
      {2, "a::Baz"},
  });
  assert(nodeIds.size() == 3);
  // Client ids come from an in-process counter seeded at MAX(id)+1 == 1 on an
  // empty db, so they are exactly 1, 2, 3 (reproducing autoincrement).
  assert(nodeIds[0] == 1 && nodeIds[1] == 2 && nodeIds[2] == 3);
  assert(storage.getNodeCount() == 3);

  // ---- batch edge insert (ids continue the same element sequence) ----------
  const auto edgeIds = storage.addEdges({
      {10, nodeIds[0], nodeIds[1]},  // Foo -> Bar
      {10, nodeIds[0], nodeIds[2]},  // Foo -> Baz
  });
  assert(edgeIds.size() == 2);
  assert(edgeIds[0] == 4 && edgeIds[1] == 5);  // element ids continue 4, 5

  // ---- IN(id-list) read ----------------------------------------------------
  const auto fromFoo = storage.getEdgesBySourceIds({nodeIds[0]});
  assert(fromFoo.size() == 2);
  const auto toBar = storage.getEdgesByTargetId(nodeIds[1]);
  assert(toBar.size() == 1);
  assert(toBar[0].sourceNodeId == nodeIds[0] && toBar[0].targetNodeId == nodeIds[1]);
  assert(toBar[0].type == 10);

  // ---- batch source-location insert (auto id) + filtered read --------------
  storage.addSourceLocations({
      {nodeIds[0], 10, 1, 10, 20, 1},  // in file node 1
      {nodeIds[0], 12, 3, 12, 8, 2},
  });
  const auto locs = storage.getSourceLocationsForFile(nodeIds[0]);
  assert(locs.size() == 2);
  assert(locs[0].fileNodeId == nodeIds[0]);
  assert(locs[0].startLine == 10 && locs[0].type == 1);

  // ---- INSERT OR IGNORE dedup on the occurrence composite key --------------
  const Id locId = locs[0].id;
  storage.addOccurrences({{nodeIds[1], locId}});
  storage.addOccurrences({{nodeIds[1], locId}});  // duplicate -> ignored
  const auto occ = storage.getOccurrencesForElementIds({nodeIds[1]});
  assert(occ.size() == 1);
  assert(occ[0].elementId == nodeIds[1] && occ[0].sourceLocationId == locId);

  std::cout << "INDEX POC PASS: batch inserts, client ids, IN-lists, "
               "count, and OR IGNORE all held.\n";
  return 0;
}
