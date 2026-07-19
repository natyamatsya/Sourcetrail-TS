# `srctrl.storage` — modularization scope

Scope for converting `src/lib/data/storage/` (45 headers, 21 `.cpp`, ~15.7k LOC) into the C++20-module
build, as the next phase after `srctrl.data`. Companion to `DESIGN_INDEXER_MODULARIZATION.md` (the
mechanics) and `DESIGN_STORAGE_CODEGEN.md` (the parallel CppSQLite3→sqlpp23 SQL-layer migration, which
this must coordinate with — see §4).

## 1. The layers (dependency order = conversion order)

| Cluster | Files | `.cpp` LOC | External coupling | Verdict |
|---|---|---|---|---|
| `type/` — POD record structs | 15 h, **0 cpp** | 0 | none (just `:types` enums + `Id`/`types.h`) | **DO FIRST — trivial, high value** |
| interface — `Storage.h`, `StorageAccess.h`, `StorageStats.h`, `StorageDbTypes.h` | ~4 h | small | abstract | easy, medium value |
| `sqlite/` — production SQLite impl | 17 h | 3787 | **CppSQLite3** + sqlpp23 schema | the sqlpp23 decision point (§3) |
| `sqlite/sqlpp23_poc/` | 11 f | 748 | **sqlpp23** (macro+template DSL) | POC being promoted; don't modularize as-is (§4) |
| `ladybug/` | 2 f | 139 | LadybugConnection | optional backend; last |
| top-level impl — `PersistentStorage` (356 h), `StorageAccessProxy/Cache/Provider`, `IntermediateStorage`, `ConcurrentStorageIndex` (459 h), `ConcurrentTursoWriter`, turso | 21 f | 6569 | turso, search indexes, graph | god-object layer; do last |

## 2. Phase S1 — `srctrl.storage:types` (the win, do first) ✅ DONE

Landed as the new `srctrl.storage` module (`srctrl_storage.cppm` + `:types` partition), `import
srctrl.data`. Two incidentals fell out: (a) `NodeAttributeKind` (an `intToEnum` enum StorageNodeAttribute
needs) was folded into `srctrl.data:types` like the other enums — a GMF `utilityEnum.h` would have clashed
with the imported `srctrl.utility`; (b) the graph core's deferred `NodeKindMask`/`NodeModifierMask` `int`
typedefs got `SRCTRL_EXPORT`ed — `StorageNode` is the first importer to *name* them. `Bookmark.h`/
`TimeStamp.h` sit cleanly in the GMF (they pull no modularized header, and no problematic std internals, so
import-std stays green). Verified all three modes: classic OFF, module ON, and `import std`.

### (original scope, for reference)

The 15 `type/` structs (`StorageNode`, `StorageEdge`, `StorageFile`, `StorageError`,
`StorageSourceLocation`, `StorageSymbol`, `StorageLocalSymbol`, `StorageOccurrence`,
`StorageComponentAccess`, `StorageElementComponent`, `StorageNodeAttribute`, `StorageBookmark*`) are
**header-only POD structs** (0 `.cpp` → no `.inl` work, just `SRCTRL_EXPORT` + `#ifndef SRCTRL_MODULE_PURVIEW`
guards). Every dependency is already modularized or GMF-global:
- classification enums (`NodeKind`, `NodeModifier`, `AccessKind`, `DefinitionKind`, `LocationType`,
  `ElementComponentKind`, `NodeAttributeKind`) → **`srctrl.data:types`** (done).
- `Id`/`types.h`, `FilePath.h` → GMF. `Bookmark.h` → `srctrl.data` (bookmark types). `Edge.h` (StorageEdge
  references `Edge::EdgeType`) → `srctrl.data:graph` (done).

So this is a **new `srctrl.storage` module with a `:types` partition** that `import srctrl.data;`. Blast
radius ~10+ TUs (indexer, serialization, the storage impls) get a clean import. Mirrors the `srctrl.data:types`
work exactly; lowest risk, highest reuse. **Recommended as the standalone next commit.**

## 3. Phase S3 — the SQLite impl and the sqlpp23-module question (the interesting part)

### Spike results (DONE — sqlpp23 modules validated on clang-22, GREEN)

A 4-step isolated spike settled every open question; all green:
1. **`sqlpp23.core` compiles as a module on clang-22** and a consumer imports it — no toolchain blocker
   (docs only claimed clang-20/gcc-15; clang-22 is fine).
2. **The name-tag macro is a non-issue — path (a) works and is simple.** A ddl2cpp-*generated-form* table
   (`struct Element_ { struct Id { SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(id,id); … }; … }; using Element =
   ::sqlpp::table_t<Element_>;`) plus a real `select(all_of(e)).from(e)` query compiled with **only**
   `import sqlpp23.core;` + a textual `#include <sqlpp23/core/name/create_name_tag.h>` (the tiny macro-only
   header). No full sqlpp `#include`s needed — the module exports enough (`table_t`, `table_columns`,
   `detail::type_set`, `integral`, `select`, `all_of`). So codegen path (b) is a *nice-to-have*, not
   required: keeping the one macro header in the GMF is already clean.
3. **The sqlite3 connector modularizes too** — a consumer that `import sqlpp23.core; import sqlpp23.sqlite3;`
   builds a `sqlpp::sqlite3::connection_config` and a query through both boundaries.
4. **Coexistence is CLEAN** (better than `import std`): a single TU that BOTH `import sqlpp23.core` AND
   `#include <sqlpp23/...>` a header compiles with 0 errors — the header-wrapper module shares the same
   global-module entities, so there's no duplicate-declaration conflict. → storage TUs need not be strictly
   import-only; integration is low-risk.

**Remaining integration work (not blockers):** (i) wire sqlpp23's `.cppm` into our build — they're not
precompiled; we compile them ourselves, and vcpkg ships the export `cxx-modules-Sqlpp23Targets.cmake` to do
it. (ii) The "experimental / evolving (v0.67)" caveat is a watch-item, but v0.69 built clean here. **Verdict:
S3 is unblocked** — the sqlite/ impl can `import sqlpp23.core/sqlite3` with the macro header in the GMF.

### (original scope)

The production `sqlite/` impls couple to two third-party libraries:
- **CppSQLite3** (`CppSQLite3.h`) — an ordinary header; lives in the **GMF** (global-module), like any
  non-modularized dep. Straightforward.
- **sqlpp23** (`<sqlpp23/...>`) — the type-safe SQL DSL. **sqlpp23 v0.69 ships module interface units**
  (`modules/sqlpp23.core.cppm`, `sqlpp23.sqlite3.cppm`, + a `cxx-modules-Sqlpp23Targets.cmake`), so storage
  could `import sqlpp23.core; import sqlpp23.sqlite3;` — a genuine "deep modules shine" showcase (consuming
  a third-party C++20 module). **Caveats, all real:**
  1. **Experimental.** sqlpp23's own docs: modules are "still evolving as of v0.67 (Sept 2025)"; tests pass
     on clang-20/gcc-15. We're on clang-22 (likely fine) but it's not a settled config.
  2. **Not precompiled.** "No compiled version of the modules will be installed; your project compiles the
     module sources itself." → we add sqlpp23's `.cppm` to our build (CMake wiring + real BMI build cost).
  3. **The `SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP` macro.** The ddl2cpp-generated schema headers define
     column/table name tags via this macro, and **macros don't cross `import`**. The macro expands to a
     `struct _sqlpp_name_tag` that does two things: (1) stringizes the SQL name (`static constexpr char
     name[] = "id"`) and (2) generates a `_member_t` with a data member **literally named** after the C++
     column (`T id = {}`) plus a `.id` accessor.
     - **Can a consteval wrapper replace it (à la the logging `fmt_with_loc`)?** Only half. The name-string
       (1) is a compile-time value that a consteval/NTTP `name_tag<"id">` could carry. But (2) *synthesizes
       a declaration with a computed identifier* (`.id`) — that's preprocessor/reflection territory;
       consteval produces values, not members-with-computed-names. So consteval alone can't replace it.
     - **It doesn't need to.** The macro runs at schema-*definition* time (in our generated header), not at
       query time, so nothing has to cross `import`. Two clean options: **(a)** `import sqlpp23.core;` for
       the machinery **+ a GMF `#include <sqlpp23/core/name/create_name_tag.h>`** for just the (tiny,
       macro-only) tag header — simple and correct; or **(b, best)** teach `tools/sqlpp23/ddl2cpp` to emit
       the tag **pre-expanded** (inline the small macro body into the generated schema) so the header is
       fully import-clean with zero macro dependency. (b) is straightforward codegen work and ties into
       `DESIGN_STORAGE_CODEGEN.md`. Whether we can drop `_member_t` entirely (if our queries address columns
       by tag *type*, not the `.id` field) is a spike question that would simplify both options.

  **Recommendation: a de-risking spike before any storage-impl conversion** — a tiny harness that
  `import sqlpp23.core; import sqlpp23.sqlite3;` + one generated table on clang-22, resolving the macro
  question (a vs b). This is the "assessment" the roadmap flagged. Only after it's green do the `sqlite/`
  impl classes (inline members, CppSQLite3 in the GMF, sqlpp23 via import).

## 4. Coordinate with the SQL-layer migration (`DESIGN_STORAGE_CODEGEN.md`)

The storage layer is **mid-migration** from hand-written CppSQLite3 to sqlpp23-generated schema — the
production `sqlite/IndexTables.h` is now itself `sqlpp23-ddl2cpp`-generated ("promoted from `sqlpp23_poc/`"),
with documented manual PK-is-FK corrections. This directly interacts with modularization:
- Modularizing the **CppSQLite3** impl now risks throwaway work if it's being replaced by sqlpp23.
- The **macro-free codegen** option (§3, option b) is a shared deliverable with the codegen migration.
- `sqlpp23_poc/` is the source the production path was promoted *from* — likely legacy; **confirm it's being
  retired and do not modularize it.**

So storage modularization should **follow, or move in lockstep with, the sqlpp23 migration** — not race ahead
of it on the CppSQLite3 code.

## 5. Recommended sequencing

1. **S1 now** — `srctrl.storage:types` (the 15 PODs). Independent of the SQL-layer churn, pure win.
2. **Spike** — validate `import sqlpp23.core/sqlite3` on clang-22 + settle the name-tag macro (§3).
3. **S2** — the abstract interface headers (`StorageAccess.h`, `Storage.h`).
4. **S3** — the `sqlite/` impl, once the spike is green and coordinated with the codegen migration.
5. **S4** — `PersistentStorage` + access/cache/provider + concurrency (turso) + ladybug, last (heaviest
   coupling; may keep impl `.cpp` classic and only modularize the interface).

Net: **S1 is an immediate, low-risk next step** identical in shape to the data-types work; everything past it
is gated on the sqlpp23-module spike and should track the SQL-layer migration rather than run ahead of it.
