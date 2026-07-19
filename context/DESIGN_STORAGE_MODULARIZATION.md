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
     column/table name tags via this macro, and **macros don't cross `import`**. Options: (a) keep
     `<sqlpp23/core/name/create_name_tag.h>` in the GMF so the macro is textually available (pragmatic), or
     (b) teach `tools/sqlpp23/ddl2cpp` to emit macro-free (pre-expanded) name tags so the schema header is
     import-clean (better, ties into `DESIGN_STORAGE_CODEGEN.md`).

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
