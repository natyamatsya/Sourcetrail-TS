# Roadmap: Zig Language Indexer

## Goal

Add first-class Zig source indexing to Sourcetrail by implementing a standalone
`sourcetrail_zig_indexer` binary that communicates with the main app over the existing
thoth-ipc / FlatBuffers IPC protocol — the same contract the C++, Rust, and Swift indexers
use. Zig is also the deliberate vehicle for a deeper research question: its compiler tracks
**per-declaration** incremental dependencies, so it is the natural place to pioneer
**sub-file incremental reindexing**, which Sourcetrail cannot do today.

This document is both a design study and a phased implementation plan. Phases 0–5 ship a
working per-file Zig indexer; the closing **sub-file incremental** section is the
de-riskable R&D follow-on.

---

## Architecture overview

```text
┌──────────────────────────────────────────────────────────────────────────┐
│  Sourcetrail (main app, C++)                                              │
│    IpcInterprocessIndexerCommandManager       ─push→  icmd_ipc_<uuid>      │
│    IpcInterprocessIntermediateStorageManager  ←push─  iist_ipc_<pid>_<uuid>│
│    IpcInterprocessIndexingStatusManager       ←────→  status channel       │
└──────────────────────────────────────────────────────────────────────────┘
        thoth-ipc (ShmHandle + IpcMutex), raw FlatBuffers bytes, chunked
┌──────────────────────────────────────────────────────────────────────────┐
│  sourcetrail_zig_indexer  (new, Zig)                                      │
│    thoth-ipc Zig port  ── opens the same SHM segments at the same names    │
│    flatcc (C, via @cImport)  ── (de)serializes the shared .fbs schemas     │
│    std.zig.Ast → ZLS Analyser/DocumentStore  ── parses/analyzes .zig       │
└──────────────────────────────────────────────────────────────────────────┘
```

The Zig indexer is a **drop-in peer** of the Rust/Swift indexers: same positional CLI
arguments, same three shared-memory channels, same `IntermediateStorage` payload. The C++
side registers metadata, launches/supervises the process, fills the command queue, and
drains the result queue. There is **no in-process C++ analysis code** — `LanguagePackageZig`
returns no indexers, exactly like `LanguagePackageRust`.

Two mature templates exist:
- **Rust** (`src/rust_indexer/`) — embeds rust-analyzer (`ra_ap_*`), closest structural
  match; its IPC module and `main.rs` loop are what the Zig side ports.
- **Swift** (`src/swift_indexer/`) — consumes the compiler's IndexStoreDB; its
  `SwiftIndexerStorageChannel.swift` hand-rolls the storage-queue framing that the Zig
  side must replicate.

---

## Why Zig is different (and the design decisions it drives)

| Dimension | C++/Rust/Swift today | Zig |
| --- | --- | --- |
| Module graph | textual `#include` (C++) / crate graph | explicit `@import` per file — a clean file→file dependency graph, no preprocessor churn |
| Change detection | mtime → content diff | same, but the explicit import graph makes reverse-dep invalidation precise |
| Incremental granularity | **whole file** | compiler tracks **per-declaration** deps (Zcu/InternPool) — strictly finer than Sourcetrail exploits |
| Transport bindings | C++/Rust/Swift ports of thoth-ipc | **native Zig port already exists** (ShmHandle, IpcMutex, byte-exact FNV-1a naming) |
| FlatBuffers codegen | flatc `--cpp/--rust/--swift` | **no flatc Zig backend** → use flatcc (C) via `@cImport` |

**Confirmed decisions:**
- **Incremental: phased.** Ship per-file first (Phase 4), matching today's model; pursue
  sub-file/per-declaration as R&D (final section).
- **Frontend: Ast-first, then ZLS.** `std.zig.Ast` prototype (Phase 3a), then ZLS
  `Analyser`/`DocumentStore` for cross-file semantics (Phase 3b) — mirrors Rust's
  syn→rust-analyzer progression.
- **Wire codec: flatcc via `@cImport`.** Generate C readers/builders from the existing
  `.fbs`, import into Zig; hand-roll only the queue framing (as Swift does).

---

## IPC contract (existing, unchanged)

Three logical channels over thoth-ipc `ShmHandle` + `IpcMutex`, each a fixed-size named
segment holding raw FlatBuffers bytes (first 4 bytes zero = empty):

| Channel | SHM name | Size | Direction |
| --- | --- | --- | --- |
| Indexer commands | `icmd_ipc_<uuid>` | 64 MiB | app → indexer |
| Intermediate storage | `iist_ipc_<pid>_<uuid>` | 16 MiB | indexer → app |
| Indexing status | status channel (`<...>_<uuid>`) | — | app ↔ indexer |

Constraints (ADRs `docs/adr/ADR-0002`, `ADR-0003`): segments are **fixed-size** (no
cross-platform SHM growth) so oversized payloads must be **chunked by the writer**;
writers emit **null (not empty) vectors** and readers **verify** buffers and log failures.
The storage queue framing is `[u64 needed_capacity][u32 count][u32 size0][bytes0]…`.

Process lifecycle: launched with positional argv
`<processId> <instanceUuid> <appPath> <userDataPath> <logFilePath>`; loop
`check interrupt → pop next Zig command (exit on none) → status → index → push (chunked) →
finalize status`. The C++ supervisor restarts the process on crash (babysitter pattern).

FlatBuffers schemas (owned by Sourcetrail-TS, shared by all indexers):
`src/lib/data/indexer/interprocess/schemas/{indexer_command,indexing_status,intermediate_storage,garbage_collector}.fbs`.

---

## Phase 0 — Scaffolding & design doc  ✅ (this doc + build flag)

- [x] `context/ROADMAP_ZIG_INDEXER.md` (this file).
- [x] CMake option `BUILD_ZIG_LANGUAGE_PACKAGE`, threaded through
  `cmake/language_packages.h.in` and `src/lib/language_package_flags.h`
  (`language_packages::buildZigLanguagePackage`).

## Phase 1 — C++ registration (mirror Rust) — ✅ landed

All edits below are in place, mirroring the Rust/Swift precedent. New `.cpp` files are
picked up automatically by the lib's `GLOB_RECURSE` (no CMake source-list edit); the fbs
enum change regenerates `IndexerCommandType_Zig` via the existing flatc step; the minimal
`IndexerCommandZig` reuses the fbs `indexed_paths` + `working_directory` fields (no new
schema fields). `SourceGroupZig` emits **one command per `.zig` file** (Zig's natural
per-file granularity), with the working directory resolved to the nearest `build.zig`.
`TaskBuildIndex` gains a Zig supervisor band trailing Swift.

Enum/type variants (add a `ZIG` case with string round-trips):
- `src/lib/settings/LanguageType.{h,cpp}` — `LanguageType::ZIG` +
  `getLanguageTypeForSourceGroupType()`.
- `src/lib/settings/source_group/SourceGroupType.{h,cpp}` — `SourceGroupType::ZIG_EMPTY` +
  `sourceGroupTypeToString` / `stringToSourceGroupType` / `...ProjectSetupString`
  (project-file string `'Zig Empty'`).
- `src/lib/data/indexer/IndexerCommandType.{h,cpp}` — `INDEXER_COMMAND_ZIG` + string maps.
- `indexer_command.fbs` — `Zig = 4` in `enum IndexerCommandType`; append Zig-specific
  command fields (`zig_exe_path`, `zig_build_args`) at the end of `IndexerCommand`
  (FlatBuffers-additive).

SourceGroup + settings (compose mixins from `src/lib/settings/source_group/component/`):
- `SourceGroupSettingsZigEmpty.{h,cpp}` (WithSourcePaths / WithSourceExtensions default
  `.zig` / WithExcludeFilters; a `WithZigOptions` mixin if command options are needed).
- `src/lib/project/SourceGroupZig.{h,cpp}` — `getAllSourceFilePaths`,
  `filterToContainedFilePaths`, `getIndexerCommands` building `IndexerCommandZig` per file
  (template: `SourceGroupRust.cpp`).
- `src/lib/project/SourceGroupFactoryModuleZig.{h,cpp}`.
- `src/lib/data/indexer/IndexerCommandZig.{h,cpp}` (+ teach `IndexerCommandSerializer`).
- `case SourceGroupType::ZIG_EMPTY` in `src/lib/settings/ProjectSettings.cpp`.

Launch/registration:
- `src/lib/app/LanguagePackageZig.{h,cpp}` — `instantiateSupportedIndexers()` → `{}`.
- `src/app/main.cpp` `addLanguagePackages()` — `addLanguagePackage<buildZigLanguagePackage,
  SourceGroupFactoryModuleZig, LanguagePackageZig>()`.
- `src/lib/app/paths/AppPath.cpp` — `getZigIndexerFilePath()` (`sourcetrail_zig_indexer`).
- `src/lib/data/indexer/TaskBuildIndex.cpp` — `runZigIndexerProcess` delegating to the
  existing `runExternalIndexerProcess(...)`; add the Zig storage-manager array + supervisor
  spawn/drain + processId band.

## Phase 2 — Zig-side transport (thoth-ipc + flatcc) — ✅ landed

- **thoth-ipc module** — the port now exposes an importable `thoth-ipc` module
  (`src/root.zig` + `b.addModule` + `build.zig.zon`); the indexer consumes it as a `path`
  dependency (`ShmHandle` / `Mutex` / `makeShmName`).
- **flatcc wire codec** — `tools/gen_flatcc.sh` strips non-ASCII (flatcc rejects the `§` in
  the shared `.fbs` comments that flatc tolerates) and runs flatcc; `build.zig` wires the
  gen step + include/link (`libflatccrt`) and `src/ipc/c.zig` `@cImport`s the bindings.
  `src/ipc/wire.zig` builds the `IntermediateStorageQueue` (one storage per entry, matching
  the C++ `queue->storages()->Get(0)` reader). **Round-trip unit-tested** (build → verify →
  read back; counts/`next_id`/name match).
- **SHM channels** — `src/ipc/shm.zig` (thoth-ipc `ShmHandle`+`Mutex`, locked read /
  read-modify-write), `src/ipc/queue.zig` (the `[u64 cap][u32 count][u32 size]…` framing,
  unit-tested), `command.zig` (pop first Zig command; writeback re-serializes the queue with
  every *other* command deep-cloned via flatcc `_clone`, preserving all fields),
  `status.zig` (full IndexingStatus RMW — interrupt read, progress + `finished_process_ids`),
  `storage_channel.zig` (push framed + back-pressure at `count() >= 2`).
- **`main.zig`** — dual mode: IPC loop (`<processId> <uuid> …` → check interrupt → pop → index
  → push → finish) and a standalone self-index driver.
- **CMake** — `BUILD_ZIG_LANGUAGE_PACKAGE` builds `sourcetrail_zig_indexer` via `zig build`
  (ReleaseSafe, `-Dschema-dir`/`-Dflatcc-prefix`) and copies it into `app/`. Verified: the
  binary builds through CMake and lands in `app/`; 12/12 Zig unit tests pass.

Remaining verification (Phase 5): live end-to-end — the app launches the indexer against a
real Zig project and the SQLite DB is populated (needs the running app; the SHM round-trip
can't be exercised by `zig build test` alone).

## Phase 3 — Analysis frontend (Ast → ZLS)

- **3a `std.zig.Ast`** — ✅ **landed** in `src/zig_indexer/` (Zig 0.16, standalone,
  `zig build test` = 7/7). `src/storage.zig` mirrors the owned `IntermediateStorage`
  (NodeKind/EdgeType/LocationType/DefinitionKind values copied from the C++ headers);
  `src/parser.zig` walks `Ast.rootDecls()` → nodes/edges/locations; `src/main.zig` is an
  idiomatic `std.process.Init` driver that self-indexes (`zig build run -- <file.zig>`;
  self-index of `parser.zig` = 30 nodes / 17 edges / 56 locations / 0 errors). Mapping
  (analogous to the Rust table in `ROADMAP_RUST_INDEXER.md`):

  | Zig construct | `NodeKind` | `EdgeType` |
  | --- | --- | --- |
  | `struct` / `enum` / `union` / `opaque` container | `NODE_STRUCT` / `NODE_ENUM` / `NODE_UNION` / `NODE_STRUCT` | — |
  | field / enum member | `NODE_FIELD` / `NODE_ENUM_CONSTANT` | `EDGE_MEMBER` from owner |
  | `fn` (free / method) | `NODE_FUNCTION` / `NODE_METHOD` | — |
  | container-scope `const` / `var` | `NODE_GLOBAL_VARIABLE` | — |
  | type alias (`const T = …`) | `NODE_TYPEDEF` | — |
  | `@import` / `@cImport` | — | `EDGE_IMPORT` (drives reverse-dep invalidation) |
  | function / method call | — | `EDGE_CALL` + occurrence |
  | type reference | — | `EDGE_TYPE_USAGE` + occurrence |
  | generic param / `comptime T: type` | `NODE_TYPE_PARAMETER` | `EDGE_MEMBER` from owner |
  | function-local binding | `local_symbol` (`file<line:col>` convention) | `LOCATION_LOCAL_SYMBOL` |

  Source locations from Ast token spans; parse errors → `StorageError`.
- **3b ZLS** (`Analyser` + `DocumentStore`): cross-file goto-def / find-refs / document
  symbols for semantic edge resolution. Comptime/type resolution is WIP — degrade to
  name-based fallback (the Rust indexer does the same for unexpanded macros). Requires
  depending on ZLS as a Zig module.

## Phase 4 — Per-file incremental (ships with the above)

No new C++ mechanism — Zig maps onto the existing model, *better* than C++ because the
module graph is explicit:
- One command per `.zig` file; `EDGE_IMPORT` edges give `getReferencing()` a precise
  reverse-dependency closure (`PersistentStorage.cpp`).
- Reuse the `file_command_hash` freshness pattern (`RefreshInfoGenerator.cpp`): store a
  Zig-derived token (resolved import set + zig version) so a compiler/flag change
  invalidates like a content change even when mtime is stale.

## Phase 5 — Testing & CI

- flatcc↔flatc round-trip (Zig serialize → C++ deserialize) — Rust analog `ipc/storage_tests.rs`.
- Parser tests over a small `.zig` fixture: node/edge kinds + exact line/col + scope extents.
- Self-index smoke (index the indexer's own sources) — Rust `index_self` analog.
- `zig build test` step in `.github/workflows/cmake-multi-platform.yml`.

---

## Design study: sub-file (per-declaration) incremental — the R&D follow-on

Sourcetrail's headline value is incremental re-index, but it is **file-granular**; Zig's
compiler hands you a **declaration-granular** dependency graph for free. Closing that gap
is the differentiating bet — and a genuine core change, scoped here but **not** built in
Phases 0–5. Three distinct lifts gate it:

1. **A persistent indexer daemon.** Today `runExternalIndexerProcess` respawns per refresh
   and exits when the queue drains, so the Zcu/DocumentStore is rebuilt every time. Sub-file
   incremental only pays off if the process (and its incremental analysis state) **survives
   between edits** — a new long-lived process mode that changes `TaskBuildIndex`'s supervisor
   contract. Prototype behind its own flag.
2. **A sub-file clear primitive.** The only clearing today is whole-file
   `removeElementsWithLocationInFiles` (`SqliteIndexStorage.cpp`). Per-declaration
   invalidation needs clear-by-symbol or clear-by-source-location-range, plus the
   `RefreshInfoGenerator` / `TaskCleanStorage` / temp-DB-swap flow taught to carry sub-file
   units.
3. **Declaration-stable IDs.** So the compiler's "these decls changed" maps to the exact
   surviving `element`/`node` rows without re-touching the rest of the file.

This connects to `ROADMAP_ANALYSIS_ENGINES.md`, which already names incremental derivation
(Differential Dataflow) as the "deep, correct end state" and notes "Sourcetrail's whole
value is incremental re-index." The Zig indexer is the natural pioneer because its compiler
is the one language frontend that emits the fine-grained graph directly.

---

## Critical files

- **Templates:** `context/ROADMAP_RUST_INDEXER.md`;
  `src/rust_indexer/indexer/src/{main.rs,ipc/*,parser/collector.rs}`;
  `src/swift_indexer/Sources/SourcetrailSwiftIndexerCore/SwiftIndexerStorageChannel.swift`.
- **Registration:** `src/lib/settings/LanguageType.*`,
  `src/lib/settings/source_group/SourceGroupType.*`, `src/lib/data/indexer/IndexerCommandType.*`,
  `src/lib/settings/ProjectSettings.cpp`, `src/app/main.cpp`,
  `src/lib/app/paths/AppPath.cpp`, `src/lib/data/indexer/TaskBuildIndex.cpp`,
  `CMakeLists.txt`, `cmake/language_packages.h.in`.
- **Wire schemas:** `src/lib/data/indexer/interprocess/schemas/*.fbs`.
- **Incremental machinery:** `src/lib/project/RefreshInfoGenerator.cpp`,
  `src/lib/project/Project.cpp`, `src/lib/data/storage/PersistentStorage.cpp`,
  `src/lib/data/storage/sqlite/SqliteIndexStorage.cpp`,
  `src/lib/data/storage/sqlite/index.sql`.
- **thoth-ipc Zig port:** `submodules/thoth-ipc/zig/thoth-ipc/src/{platform/shm.zig,sync/mutex.zig,platform/shmname.zig}`
  (populated in the primary checkout; empty in this worktree — build from the primary
  checkout or a git worktree, per the submodule caveat).

## Open questions / risks

- **ZLS semantic maturity** — comptime/type resolution is WIP; 3b must degrade gracefully.
- **flatcc as a new build dependency** — vcpkg overlay or FetchContent; verify it coexists
  with the existing vcpkg `flatbuffers`/`flatc`.
- **thoth-ipc Zig module packaging** — an upstream change to the submodule (pin-dance applies).
- **Persistent-daemon process model** — the biggest unknown; prototype behind a flag before
  committing.
