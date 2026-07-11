# sqlpp23 storage migration — design & the base-class refactor

Sourcetrail persists its index and bookmarks in SQLite through a hand-written
wrapper (`CppSQLite3DB`) and raw SQL strings. This document scopes migrating the
SQLite storage layer to [sqlpp23](https://github.com/rbock/sqlpp23) (a type-safe
SQL DSL), and — the centerpiece — the **`SqliteStorage` base-class refactor** that
lets the two concrete storages (`SqliteIndexStorage`, `SqliteBookmarkStorage`)
move onto sqlpp23 incrementally while sharing **one** `sqlite3` handle.

It is a plan of record for the refactor, not a description of shipped code. The
enabling pieces below already exist and are proven; the base-class change is the
next step.

## Groundwork already in place

| Piece | Where | State |
|---|---|---|
| vcpkg overlay port for sqlpp23 0.69 | `vcpkg-overlay-ports/sqlpp23/` | installs clean under `--enforce-port-checks` |
| Vendored `ddl2cpp` table generator (+ reserved-word fix) | `tools/sqlpp23/ddl2cpp/` | regression-tested |
| Bookmark storage POC (typed CRUD, IoC) | `src/lib/data/storage/sqlite/sqlpp23_poc/` | compiles + runs (`POC PASS`) |
| Meta/version/transaction POC | same | `POC PASS` |
| Index storage POC (batch insert, client-ids, IN-lists) | same | `INDEX POC PASS` |

The POCs proved the *mechanics* on standalone connections. What they did **not**
do — and what this refactor is about — is put a real `SqliteStorage` on an
injected connection so production code can migrate statement-by-statement.

## The one hard decision: who owns the `sqlite3` handle

sqlpp23's `sqlite3::connection` owns its `sqlite3*` via a `unique_ptr` with a
**function-pointer deleter**, and `detail::connection_handle`'s members are
public. That means a `sqlpp::sqlite3::connection` can **borrow** an existing
handle non-owningly (no-op deleter) through a tiny `common_connection<connection_base>`
shim — validated during the POC work. This unlocks a **one-handle, two-views**
design instead of two connections (which would split transaction scope):

```
                 ┌─────────────────────────────┐
                 │  one sqlite3* (owned once)   │
                 └───────────────┬─────────────┘
              legacy view        │        migrated view
        CppSQLite3DB API   ◄─────┴─────►  sqlpp::sqlite3::connection (borrowed)
   execDML/execQuery/compileStatement    db(select(...)), db(insert_into(...)...)
```

Both views share the same connection, so transactions, `PRAGMA`s, `WAL`, and the
meta table are consistent no matter which view a given statement uses. Un-migrated
raw SQL and new typed queries coexist on one handle for the whole migration.

**Rejected alternative:** give each storage its own sqlpp23 connection. Two handles
to one file means the base class's `BEGIN/COMMIT` (raw) and a typed `insert` land in
different transaction scopes — the exact hazard the bookmark POC calls out.

## Where it plugs into the existing seam

The storage layer already refers to the backend **only** through the aliases in
`src/lib/data/storage/sqlite/StorageDbTypes.h`:

```cpp
using StorageDb    = CppSQLite3DB;         // or DualCppSqlite3DB under SOURCETRAIL_TURSO_COMPARE
using StorageStmt  = CppSQLite3Statement;
using StorageQuery = CppSQLite3Query;
```

That alias is the injection point. The refactor adds a sqlpp23 view **alongside**
the existing `StorageDb`, reached from `SqliteStorage`, without disturbing the
alias contract the ~64 raw call sites depend on.

### Raw call-site inventory (what must keep working unchanged)

| Surface | `SqliteIndexStorage` | `SqliteBookmarkStorage` |
|---|---|---|
| `m_database.execDML` | 26 | 10 |
| `m_database.compileStatement` | 10 | 4 |
| `m_database.lastRowId` | 2 | 4 |
| wrapper `executeStatement/Query/Scalar` | ~60 | ~13 |
| `hasTable` / `getMetaValue` / `insertOrUpdateMetaValue` | 1 / 1 / 2 | via base |

Definitions: `SqliteStorage.{h,cpp}` (ctor/dtor `SqliteStorage.cpp:7–37`,
helpers `162–289`, pragmas `291–313`). `CppSQLite3DB`: `src/external/sqlite/CppSQLite3.h:131–166`
(note: `sqlite3* mpDB` is **private with no accessor** — Phase 0 adds one).

## Target architecture (end state)

`SqliteStorage` receives an injected **connection facade** instead of opening a
`FilePath` itself:

```cpp
// Composition root — PersistentStorage.cpp
auto conn = StorageConnection::open(dbPath);          // owns the one sqlite3 handle
m_sqliteIndexStorage    = SqliteIndexStorage(conn.indexView());
m_sqliteBookmarkStorage = SqliteBookmarkStorage(bookmarkConn.view());
```

`StorageConnection` owns the handle and exposes both views:

```cpp
class StorageConnection {
 public:
  static std::optional<StorageConnection> open(const FilePath&);
  StorageDb&                    legacy();   // CppSQLite3DB (or Dual) — raw path
  sqlpp::sqlite3::connection&   typed();    // borrows legacy()'s handle
  // lifecycle: close(), reopen(), used around the temp-DB swap
};
```

`SqliteStorage` holds a **reference/pointer** to the facade instead of a
`mutable StorageDb m_database` value; subclasses keep calling the same protected
helpers (`executeStatement`, …) which now forward to `facade.legacy()`, and new
code calls `facade.typed()`.

Production DI surface is tiny: the **single** instantiation site is
`PersistentStorage`'s ctor (`PersistentStorage.cpp:41–42`). The bulk of churn is
the ~8 test instantiation sites, which move from `Storage s(path)` to
`auto c = StorageConnection::open(path); Storage s(c.view());`.

## Phased plan (build stays green at every step)

**Phase 0 — expose the handle (no behavior change).**
Add `sqlite3* CppSQLite3DB::handle()` (and to `DualCppSqlite3DB`, returning its
inner SQLite handle). Add the `common_connection<connection_base>` borrow shim
(from the POC) as a small header. Nothing consumes it yet. *Ships independently.*

**Phase 1 — dual-view without touching call sites.**
Keep the `SqliteStorage(FilePath)` ctor; internally it still opens `m_database`,
but also constructs a borrowed `sqlpp::sqlite3::connection` over
`m_database.handle()` and exposes `sqlpp::sqlite3::connection& db()` (protected).
All existing raw call sites are unchanged. Verifies the shared handle end-to-end
against the existing test suites.

**Phase 2 — migrate the base-class meta/version plumbing.**
Convert `getMetaValue`/`insertOrUpdateMetaValue`/`hasTable`/`setVersion`/`setTime`
and `begin/commit/rollbackTransaction` to `db()` (typed), per the meta POC. This
exercises the shared handle on real base-class code shared by both storages.
Model the `meta` table with the fixed `ddl2cpp` (note: `key` is a reserved word —
the vendored generator now keeps it).

**Phase 3 — migrate `SqliteBookmarkStorage`.**
Port its ~11 methods to typed queries (bookmark POC is the template). Smallest
concrete storage; low call volume; a clean full-class win to validate the pattern
on production types.

**Phase 4 — migrate `SqliteIndexStorage` (biggest payoff first).**
Lead with the **batch inserts**: `insert_into(t).columns(...)` + `add_values(...)`
replaces the entire `InsertBatchStatement<T>` 999-parameter machinery (direct
execution inlines values, so the bound-parameter limit that forced the dance is
moot). Then client-side id allocation, IN-list reads, `INSERT OR IGNORE`, and the
`forEach<T>` family. Correct the FK-primary-key `id`s in the generated tables
(`[PK-is-FK]`: edge/node/symbol/file/filecontent/local_symbol/error).

**Phase 5 — flip to true injection, retire the `FilePath` ctor.**
Move handle ownership to `StorageConnection`, injected by `PersistentStorage`;
update the ~8 test sites; drop `SqliteStorage`'s open/close from ctor/dtor. Once no
raw call sites remain in a class, its `CppSQLite3DB` dependency can go; `CppSQLite3DB`
itself is retired when both storages are fully typed (or kept only for the Dual
backend).

Each phase compiles, passes the existing `SqliteIndexStorageTestSuite` /
`SqliteBookmarkStorageTestSuite` / `StorageTestSuite`, and leaves raw + typed code
coexisting.

## Complications addressed head-on

- **Temp-DB-then-swap lifecycle.** Indexing writes to a temp DB, then
  `Project::swapToTempStorageFile` (`Project.cpp:833–857`) `remove()`s the live
  file and `rename()`s the temp over it — which fails if the handle is still open.
  DI makes this *cleaner*: the composition root already controls handle lifetime,
  so "close facade → swap files → reopen → re-inject" is explicit at the `Project`
  level rather than implicit in object lifetime. Phase 5 must sequence this; Phases
  1–4 keep today's open-in-ctor lifetime and are unaffected.
- **`SOURCETRAIL_TURSO_COMPARE` (Dual backend).** `DualCppSqlite3DB` runs SQLite +
  Turso in lockstep and diffs them (`turso_compare::divergenceCount()`). The typed
  view borrows the **SQLite** handle only (the source of truth); the Turso-compare
  path stays on the raw Dual API. So typed migration and Turso comparison are
  orthogonal — do not try to route sqlpp23 through the Dual wrapper. Phase 0 adds
  the SQLite-handle accessor to `DualCppSqlite3DB`.
- **`SOURCETRAIL_USE_LADYBUG` / `SOURCETRAIL_TURSO_CONCURRENT`.** Both mirror to
  *separate* files (`.lbug`, `.concurrent.turso`) via their own factories
  (`PersistentStorage.cpp:62–128`). Untouched by this refactor.
- **`SOURCETRAIL_CLIENT_IDS`.** The typed `insertElement()` seeds an in-process
  counter from `select(max(element.id))` — proven in the index POC. The flag's
  read-back-free dedup sets (`m_knownFilePaths`, `m_errorDedup`) are in-memory and
  backend-independent; they carry over verbatim.
- **Escaping / performance.** Direct-executed typed statements inline values;
  sqlpp23 escapes them correctly (not naive concatenation). For very large batches,
  chunk by statement length rather than a hard 999. Prepared statements remain
  available where bound parameters are preferred.
- **Toolchain.** sqlpp23 needs real Clang ≥ 20.1 / GCC ≥ 14.2 / MSVC ≥ 19.44.
  Standardize on the `llvm-clang-*` CMake presets; Apple clang's version numbers do
  not map to upstream Clang.

## Testing strategy

- The existing `SqliteIndexStorageTestSuite`, `SqliteBookmarkStorageTestSuite`, and
  `StorageTestSuite` are the regression net; every phase must keep them green.
- `SOURCETRAIL_TURSO_COMPARE` doubles as a **differential oracle**: with SQLite as
  source of truth, a migrated statement that diverges from the raw path shows up in
  `divergenceCount()`.
- Index a mid-size real project (ripgrep ~52K LOC, tokio ~159K LOC per the Turso
  notes) and diff the resulting `.srctrldb` against a pre-migration build for
  byte/row equivalence.

## Open questions to confirm before Phase 5

1. **Facade granularity** — one `StorageConnection` per file (index vs bookmark are
   already separate files/handles), or a shared owner? Recommendation: per file,
   matching today's topology.
2. **Retire `CppSQLite3DB`?** Keep it indefinitely for the Dual backend, or drop it
   once both storages are typed and Turso-compare is reworked separately?
3. **Prepared vs inlined** for the hot insert path at scale — measure on tokio
   before committing to inlined multi-row inserts as the default.

## Effort & sequencing

Phases 0–2 are small and self-contained (handle accessor + shim + base-class meta),
landable in one PR each. Phase 3 is a bounded full-class port. Phase 4 is the large
one (index storage) but front-loads the biggest simplification (batch inserts).
Phase 5 is mechanical but touches every instantiation site and the swap lifecycle,
so it lands last. Nothing here blocks ongoing indexing work; raw and typed coexist
throughout.
