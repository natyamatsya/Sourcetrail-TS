# sqlpp23 bookmark-storage proof-of-concept

Reimplements `SqliteBookmarkStorage` on top of [sqlpp23](https://github.com/rbock/sqlpp23),
the type-safe SQL DSL, to validate the migration approach before touching the
real storage layer. **Compiles and runs green** against the sqlpp23 vcpkg
overlay port (`vcpkg-overlay-ports/sqlpp23`).

## Files

| File | What it is |
|------|-----------|
| `BookmarkTables.h` | The 5 bookmark tables as sqlpp23 structs (ddl2cpp output format), written by hand from the existing `CREATE TABLE` DDL. |
| `SqliteBookmarkStorageSqlpp23.{h,cpp}` | The converted storage class — same public API as `SqliteBookmarkStorage`, typed queries inside. |
| `MetaTable.h` / `meta.sql` | The `meta` key/value table (sqlpp23 struct + its DDL). |
| `SqliteStorageMetaSqlpp23.{h,cpp}` | The base-class meta/version/time/transaction plumbing from `SqliteStorage`, converted. |
| `poc_main.cpp` | Standalone driver: builds one connection, injects it, exercises every method, asserts. |
| `CMakeLists.txt` | Standalone build (not wired into the main build). |

## The key decision: inversion of control

sqlpp23's `connection` **owns** its `sqlite3*` handle (via a `unique_ptr` with a
`sqlite3_close` deleter). There is no public "wrap this existing handle"
constructor. So the choice is:

- ❌ Let this class open its *own* second connection to the bookmark DB file →
  two handles to one file, **separate transaction scopes**. Rejected.
- ✅ **Inject the connection** through the constructor
  (`SqliteBookmarkStorageSqlpp23(sqlpp::sqlite3::connection& db)`). Whoever owns
  the bookmark database owns the one connection; this class borrows it.

That is the inversion of control: the composition root (see `poc_main.cpp`)
creates and owns the single connection, then hands it in. Schema/pragma DDL and
the typed CRUD then run on the **same** handle, so they share one transaction
scope — exactly the property the current `CppSQLite3DB`-based code relies on.

### Unified single-handle migration path

`setupTables()` shows the migration escape hatch: statements not yet expressed
in the DSL (here, `CREATE TABLE`) run as raw SQL through the *same* injected
connection via `db("...")` — sqlpp23's `operator()(std::string_view)`. So a
half-migrated class keeps one connection while some statements are typed and
some are still raw. No second handle, no split transactions.

## How this generalizes to the real code

The real `SqliteBookmarkStorage` derives from `SqliteStorage`, which today
constructs and owns a `CppSQLite3DB m_database`. The end-state refactor is to
invert that: `SqliteStorage` receives an injected connection abstraction instead
of hard-constructing `CppSQLite3DB`. Because the bookmark and index databases
are already *separate files with separate connections*, each storage still owns
exactly one connection — nothing about the current architecture fights this.

Migration can then proceed statement-by-statement: raw SQL keeps working through
`db("...")` while individual queries are converted to the typed DSL, all on the
one shared handle.

## Meta / version handling (base-class plumbing)

`SqliteStorageMetaSqlpp23` converts the shared `SqliteStorage` logic: the `meta`
key/value table, `getVersion`/`setVersion`, `setTime`/`getTime`, `hasTable`, and
the transaction methods. Notable points:

- **Upsert without a UNIQUE constraint.** The original faked an upsert with
  `INSERT OR REPLACE INTO meta(id,key,value) VALUES((SELECT id FROM meta WHERE
  key=?), ?, ?)` — a workaround because `meta` has no `UNIQUE(key)`. The typed
  version is a clear read-then-`update`-or-`insert`. (Add `UNIQUE(key)` and it
  collapses to sqlpp23's `insert_or_replace()` / `on_conflict(...).do_update()`.)
- **`hasTable` is now bound, not concatenated.** `sqlite_master` is modelled as a
  tiny local table so the check is `where(type == "table" and name == ?)` with a
  bound name, instead of interpolating the name into the SQL string.
- **Transactions** map directly onto the connection's native
  `start_transaction()` / `commit_transaction()` / `rollback_transaction()`.

This matters because `SqliteStorage` is the base of *both* the bookmark and index
storages — converting it is what lets the whole hierarchy move to the injected
connection.

## Index storage (focused conversion of the novel parts)

`SqliteIndexStorageSqlpp23` + `IndexTables.h` + `index_poc_main.cpp` cover the
parts of `SqliteIndexStorage` that the bookmark POC did *not* already prove.
Build/run the `index_sqlpp23_poc` target; it prints `INDEX POC PASS`.

- **Batch multi-row INSERT replaces `InsertBatchStatement<T>` wholesale.** The
  hand-rolled machinery (compile statements at batch sizes 333/166/.../1 to stay
  under SQLite's 999 *bound-parameter* limit, plus per-column bind lambdas) becomes
  `insert_into(t).columns(...)` + a loop of `.add_values(...)` + one `db(insert)`.
  Direct execution **inlines** the values (properly escaped), so the 999-parameter
  ceiling that motivated the dance does not apply. Very large batches would chunk
  by statement length, not by a hard variable count — a much simpler constraint.
- **Client-side id allocation** (`SOURCETRAIL_CLIENT_IDS`): `insertElement()` mints
  ids from an in-process counter seeded via `select(max(element.id))`, reproducing
  autoincrement (the POC asserts ids come out 1,2,3 then 4,5). No `lastRowId`
  round-trip.
- **`IN(id-list)` reads** — `edge.sourceNodeId.in(std::vector<int64_t>{...})`,
  replacing `"... IN (" + join(toStrings(ids), ',') + ")"`.
- **`INSERT OR IGNORE`** — `sqlpp::sqlite3::insert_or_ignore().into(occurrence)...`
  for the occurrence composite-key dedup.
- **Aggregate** — `select(count(node.id).as(cnt))`.

Deliberately **out of scope** for this focused pass (all mechanical repetition of
patterns already proven above): the ~70 remaining reads/removes/counts, the
in-memory dedup temp-indices (`m_tempNodeNameIndex` etc. — orthogonal, backend
independent), and the `forEach<T>` template family (maps onto the same typed
`select(all_of(t)).from(t).where(...)` loop). Five of the 13 index tables have an
`id` that is a foreign key; only `node`/`edge` are corrected here (see the
`[PK-is-FK]` note in `IndexTables.h`), the rest are flagged for the full pass.

## What the tooling already caught

Two silent-data-loss / latent-bug classes surfaced just from generating the
headers and compiling — before running anything:

1. **`ddl2cpp` silently dropped the `meta.key` column** (`[reserved-word]` in
   `MetaTable.h`). `key` is a SQL keyword, so the generator's grammar skipped it
   and emitted a `meta` model with only `id`/`value` — the column the table is
   keyed on, gone with no error. Hand-added and flagged. (Now fixed in the
   vendored generator, `tools/sqlpp23/ddl2cpp`, patch `[reserved-word-column]`.)
2. **`--assume-auto-id` mis-marked FK primary keys** (`[PK-is-FK]` in
   `BookmarkTables.h`) — see above.

Both are things a codegen step for the full 19-table migration must guard
against (rename reserved-word columns or post-patch; fix auto-id on FK PKs).

## What the compiler already caught

Writing the tables, sqlpp23's `static_assert` rejected an `insert` that omitted a
NOT NULL column with no default (`assert_all_required_assignments_t`). That class
of bug — a required column silently missing — is a runtime `NOT NULL constraint
failed` today; here it fails to compile. That is the whole point of the move.

Concretely improved over the original:

- `updateBookmark()` was **three** separate string-concatenated `UPDATE`
  statements (with `name`/`comment` interpolated straight into SQL — an
  injection risk). It is now **one** typed, parameter-bound statement.
- Every `WHERE`/`VALUES` that was built with `+ to_string(...)` is now a bound,
  type-checked expression.

## Build & run

Requires the sqlpp23 vcpkg port installed (see `vcpkg-overlay-ports/sqlpp23`) and
an LLVM clang ≥ 20.1 (Apple clang won't do — sqlpp23 needs real C++23).

```sh
cmake -S . -B build -G Ninja \
    -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
    -DCMAKE_TOOLCHAIN_FILE=<repo>/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_TARGET_TRIPLET=arm64-osx \
    -DVCPKG_INSTALLED_DIR=<install-root> -DVCPKG_MANIFEST_MODE=OFF
cmake --build build && ./build/bookmark_sqlpp23_poc
# -> POC PASS: all sqlpp23 bookmark-storage assertions held.
```

> Note: the repo's universal `x64-arm64-linux-windows-osx-static-md` triplet
> produces a fat `libsqlite3.a` that the macOS linker rejects
> (`archive member '/' not a mach-o file`) — a triplet artifact unrelated to
> sqlpp23. Use a single-arch triplet (e.g. `arm64-osx`) or link the system
> sqlite3 for local runs.
