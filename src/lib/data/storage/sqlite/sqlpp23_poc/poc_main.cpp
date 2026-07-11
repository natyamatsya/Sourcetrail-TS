// Standalone driver proving the sqlpp23 bookmark-storage POC compiles and runs.
//
//   - Constructs ONE owning sqlpp23 connection (the "composition root").
//   - Injects it into SqliteBookmarkStorageSqlpp23 (inversion of control).
//   - Exercises the full public API and asserts on the results.
//
// Build/run: see CMakeLists.txt in this directory.

#include <cassert>
#include <iostream>
#include <memory>

#include <sqlpp23/sqlite3/database/connection.h>
#include <sqlpp23/sqlite3/database/connection_config.h>

#include "SqliteBookmarkStorageSqlpp23.h"
#include "SqliteStorageMetaSqlpp23.h"

using namespace bm_poc;

int main() {
  // ---- composition root: create & own the single connection ----------------
  auto config = std::make_shared<sqlpp::sqlite3::connection_config>();
  config->path_to_database = ":memory:";
  config->flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
  sqlpp::sqlite3::connection db{config};
  db("PRAGMA foreign_keys=ON");

  // ---- meta / version plumbing (shared base-class logic), same connection --
  meta_poc::SqliteStorageMetaSqlpp23 meta{db};
  meta.setupMetaTable();
  assert(meta.hasTable("meta"));
  assert(!meta.hasTable("does_not_exist"));

  assert(meta.getVersion() == 0);  // empty DB
  meta.setVersion(25);
  assert(meta.getVersion() == 25);
  meta.setVersion(26);  // upsert path (key already present)
  assert(meta.getVersion() == 26);

  meta.setTime("2026-07-11 12:00:00");
  assert(meta.getTime() == "2026-07-11 12:00:00");
  assert(meta.getMetaValue("nonexistent").empty());

  // transactions map onto the connection's native API
  meta.beginTransaction();
  meta.insertOrUpdateMetaValue("scratch", "x");
  meta.commitTransaction();
  assert(meta.getMetaValue("scratch") == "x");

  // ---- inject the same connection into the bookmark storage (IoC) ----------
  SqliteBookmarkStorageSqlpp23 storage{db};
  storage.setupTables();

  // ---- categories ----------------------------------------------------------
  const auto workCat = storage.addBookmarkCategory({"Work"});
  const auto todoCat = storage.addBookmarkCategory({"Todo"});
  assert(workCat.id != 0 && todoCat.id != 0 && workCat.id != todoCat.id);

  auto categories = storage.getAllBookmarkCategories();
  assert(categories.size() == 2);

  const auto found = storage.getBookmarkCategoryByName("Todo");
  assert(found.id == todoCat.id && found.name == "Todo");

  // ---- bookmarks -----------------------------------------------------------
  const auto bm1 = storage.addBookmark({"Fix parser", "urgent", "2026-07-11 09:00:00", workCat.id});
  const auto bm2 = storage.addBookmark({"Write docs", "", "2026-07-11 10:00:00", todoCat.id});
  assert(bm1.id != 0 && bm2.id != 0);

  auto bookmarks = storage.getAllBookmarks();
  assert(bookmarks.size() == 2);

  // ---- update (single typed statement replacing 3 string-built UPDATEs) ----
  storage.updateBookmark(bm1.id, "Fix parser bug", "very urgent", todoCat.id);
  for (const auto& b : storage.getAllBookmarks()) {
    if (b.id == bm1.id) {
      assert(b.name == "Fix parser bug");
      assert(b.comment == "very urgent");
      assert(b.categoryId == todoCat.id);
    }
  }

  // ---- bookmarked nodes (INSERT into element + node, join on read) ---------
  const auto node1 = storage.addBookmarkedNode({BookmarkId(bm1.id), "com::example::Foo"});
  assert(node1.id != 0);
  auto nodes = storage.getAllBookmarkedNodes();
  assert(nodes.size() == 1);
  assert(nodes[0].serializedNodeName == "com::example::Foo");
  assert(nodes[0].bookmarkId == bm1.id);

  // ---- bookmarked edges ----------------------------------------------------
  const auto edge1 =
      storage.addBookmarkedEdge({BookmarkId(bm2.id), "src::A", "dst::B", 7, true});
  assert(edge1.id != 0);
  auto edges = storage.getAllBookmarkedEdges();
  assert(edges.size() == 1);
  assert(edges[0].serializedSourceNodeName == "src::A");
  assert(edges[0].serializedTargetNodeName == "dst::B");
  assert(edges[0].edgeType == 7);
  assert(edges[0].sourceNodeActive == true);

  // ---- delete + cascade ----------------------------------------------------
  storage.removeBookmark(bm1.id);
  assert(storage.getAllBookmarks().size() == 1);
  // bm1's bookmarked_element/node cascade away (FK ON DELETE CASCADE).
  assert(storage.getAllBookmarkedNodes().empty());

  storage.removeBookmarkCategory(todoCat.id);
  assert(storage.getAllBookmarkCategories().size() == 1);

  std::cout << "POC PASS: all sqlpp23 bookmark-storage assertions held.\n";
  return 0;
}
