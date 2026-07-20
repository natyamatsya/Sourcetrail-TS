# Roadmap: Rust Language Indexer

## Goal

Add first-class Rust source indexing to Sourcetrail by implementing a standalone
`sourcetrail_rust_indexer` binary that communicates with the main app over the
existing thoth-ipc / FlatBuffers IPC protocol — the same protocol used by the C++
indexer today.

---

## Architecture Overview

```text
┌─────────────────────────────────────────────────────────────┐
│  Sourcetrail (main app, C++)                                │
│                                                             │
│  InterprocessIndexerCommandManager  ──push──►  SHM: icmd_ipc_<uuid>   │
│  InterprocessIntermediateStorageManager  ◄─push──  SHM: istorage_ipc_<uuid> │
│  InterprocessIndexingStatusManager  ◄──────►  SHM: istatus_ipc_<uuid> │
└─────────────────────────────────────────────────────────────┘
         shared memory (thoth-ipc channels, FlatBuffers payload)
┌─────────────────────────────────────────────────────────────┐
│  sourcetrail_rust_indexer  (new, Rust)                      │
│                                                             │
│  libipc (Rust crate)  ──opens same SHM channels            │
│  flatbuffers (Rust crate)  ──deserializes commands,         │
│                              serializes results             │
│  ra_ap_* / rust-analyzer libraries  ──parses .rs files      │
└─────────────────────────────────────────────────────────────┘
```

The Rust indexer is a **drop-in peer** of the C++ indexer: it speaks the same
wire protocol, is launched with the same CLI arguments, and produces the same
`IntermediateStorage` FlatBuffers payload. No changes to the app's storage or
UI layers are needed.

---

## IPC Protocol (existing, unchanged)

### Shared memory channel names

| Channel | SHM name prefix | Owner |
| --- | --- | --- |
| Indexer commands | `icmd_ipc_<uuid>` | App (creates) |
| Intermediate storage | `istorage_ipc_<uuid>` | App (creates) |
| Indexing status | `istatus_ipc_<uuid>` | App (creates) |
| Garbage collector | `igc_ipc_<uuid>` | App (creates) |

### Wire format

Each channel stores a **size-prefixed FlatBuffers table** in the SHM region.
The first 4 bytes are a little-endian `uint32` byte count (standard FlatBuffers
file prefix). All zeros means "empty / no data".

### FlatBuffers schemas (already defined)

- `abi-schemas/ipc-indexer/indexer_command.fbs`
  — `IndexerCommandQueue` / `IndexerCommand` / `IndexerCommandType`
- `abi-schemas/ipc-indexer/intermediate_storage.fbs`
  — `IntermediateStorageQueue` / `IntermediateStorage` and all sub-tables
- `abi-schemas/ipc-indexer/indexing_status.fbs`
  — `IndexingStatus` / `ProcessFile`
- `abi-schemas/ipc-indexer/garbage_collector.fbs`
  — `GarbageCollectorState`

### Indexer process lifecycle

The app launches the indexer binary with positional CLI arguments:

```text
sourcetrail_rust_indexer <processId> <instanceUuid> <appPath> <userDataPath> <logFilePath>
```

The indexer then:

1. Opens the four SHM channels (OPEN_OR_CREATE, not owner).
2. Polls `icmd_ipc_<uuid>` for `IndexerCommand` entries with `type == Rust`.
3. For each command: updates status → indexes file → pushes `IntermediateStorage` → finalises status.
4. Exits when the command queue is empty or `indexing_interrupted` is set.

---

## Phase 1 — Schema & FlatBuffers Rust bindings

**Goal:** generate Rust types from the existing `.fbs` schemas.

### Phase 1 Tasks

- [x] Add `IndexerCommandType::Rust = 2` to `indexer_command.fbs`.
- [x] Add a `build.rs` in the Rust indexer crate that invokes `flatc --rust`
      on all four schemas and places generated code under `src/generated/`.
- [x] Verify round-trip: serialize an `IntermediateStorage` in Rust, deserialize
      in C++ (unit test — see `ipc/storage_tests.rs`).

### Key crate

```toml
flatbuffers = "25.12.19"   # matches the vcpkg flatbuffers version in use
```

---

## Phase 2 — IPC layer (Rust side)

**Goal:** open and use the thoth-ipc SHM channels from Rust.

### Phase 2 Tasks

- [x] Add the `libipc` Rust crate (already at
      `submodules/thoth-ipc/rust/libipc/`) as a workspace member or path
      dependency of the indexer crate.
- [x] Implement `CommandChannel` wrapper:
  - `pop_rust_command() -> Option<OwnedIndexerCommand>` — reads + deserializes
    from `icmd_ipc_<uuid>`, filters by `IndexerCommandType::Rust`, writes back
    the shortened queue preserving original command types.
- [x] Implement `StorageChannel` wrapper:
  - `push(storage)` — writes serialized `IntermediateStorage` into
    `istorage_ipc_<uuid>`.
  - `storage_count() -> usize` — reads queue length (back-pressure guard).
- [x] Implement `StatusChannel` wrapper:
  - `start_indexing(path)`, `finish_indexing()`, `is_interrupted() -> bool`.

### SHM access pattern

Use `libipc::Channel` / `libipc::Route` with the same name prefix strings as
the C++ managers. The SHM mutex/lock protocol is already handled by thoth-ipc
internally.

---

## Phase 3 — Rust parser / symbol extractor

**Goal:** parse `.rs` source files and emit a populated `IntermediateStorage`.

### Recommended approach: `ra_ap_*` crates (rust-analyzer public API)

```toml
ra_ap_syntax    = "*"   # CST / AST
ra_ap_hir       = "*"   # name resolution, type inference
ra_ap_ide       = "*"   # higher-level queries (go-to-def, find-refs)
```

These crates give access to the same analysis rust-analyzer uses, including
cross-file resolution. They are published to crates.io from the rust-analyzer
monorepo.

### Alternative (simpler, single-file only): `syn`

```toml
syn = { version = "2", features = ["full", "visit"] }
```

`syn` is easier to integrate but has no cross-file name resolution. Suitable
for a first working prototype.

### Symbol mapping to Sourcetrail node/edge kinds (as implemented)

| Rust construct | `NodeKind` | `EdgeType` |
| --- | --- | --- |
| `mod` | `NODE_MODULE` | — |
| `struct` / `enum` / `union` | `NODE_STRUCT` / `NODE_ENUM` / `NODE_UNION` | — |
| field / enum variant | `NODE_FIELD` / `NODE_ENUM_CONSTANT` | `EDGE_MEMBER` from owner |
| `trait` | `NODE_INTERFACE` | — |
| `impl Trait for Type` | — | `EDGE_INHERITANCE` (Type → Trait) |
| supertrait (`trait A: B`) | — | `EDGE_INHERITANCE` (A → B) |
| impl method of a trait | — | `EDGE_OVERRIDE` (impl fn → trait fn) |
| `fn` (free / method) | `NODE_FUNCTION` / `NODE_METHOD` | — |
| generic param (`T`, `'a`, `const N`) | `NODE_TYPE_PARAMETER` | `EDGE_MEMBER` from owner |
| trait/lifetime bound (`T: Trait`, `'b: 'a`) | — | `EDGE_TYPE_USAGE` from the *param* node |
| function / method call | — | `EDGE_CALL` + occurrence at call site |
| value ref (const/static/variant), field access | — | `EDGE_USAGE` + occurrence |
| type reference | — | `EDGE_TYPE_USAGE` + occurrence |
| generic use (`Holder<Foo>`) | `Holder<Foo>` implicit node (§7, scope `local`) | `EDGE_TEMPLATE_SPECIALIZATION` → `Holder`, `EDGE_TYPE_ARGUMENT` → `Foo` |
| `const` / `static` | `NODE_GLOBAL_VARIABLE` | — |
| `type` alias | `NODE_TYPEDEF` | — |
| `macro_rules!` definition | `NODE_MACRO` | — |
| `use` item | — | `EDGE_IMPORT` (importing scope → imported def) |
| macro invocation (bang / derive / attribute) | external macro → `NODE_MACRO` (`DefinitionKind::NONE`) | `EDGE_MACRO_USAGE` (file node → macro def) |

Reference occurrences attach to the **edge id** (mirroring the C++
`ParserClientImpl::recordReference`); definitions carry a name-token
location plus a `LOCATION_SCOPE` spanning the full item. Function-local
bindings are recorded as local symbols (`file<line:col>` name convention,
`LOCATION_LOCAL_SYMBOL` occurrences) matching
`CxxAstVisitorComponentIndexer::getLocalSymbolName`.
See `context/DESIGN_RUST_TYPE_SYSTEM_EDGES.md` for the bound/lifetime/
type-argument/override model.

### Phase 3 Tasks

- [x] Implement `RustIndexer::index(command) -> IntermediateStorage` using
      `syn` for a prototype (single-file, no cross-file resolution).
- [x] Wire up source locations (`start_line`, `start_col`, `end_line`,
      `end_col`) from `proc_macro2::Span` (with `span-locations` feature).
- [x] Upgrade to `ra_ap_*` for cross-file resolution (Phase 3b — complete).
      `load_workspace_at()` + `Module::declarations()` + `Module::legacy_macros()`
      + `EditionedFileId::parse_errors()`. `index_crate(working_directory)`
      used in production binary. Self-indexing (2026-07-10): 46 files,
      1012 nodes, 3832 locations, 0 errors.
- [x] Handle `StorageError` entries for parse failures.

---

## Phase 4 — Binary & build system integration

**Goal:** produce `sourcetrail_rust_indexer` and integrate it into the CMake
build so it is placed alongside `Sourcetrail_indexer` in the output `app/`
directory.

### Phase 4 Tasks

- [x] Create `src/rust_indexer/` Cargo workspace.
- [x] Add CMake `corrosion` integration that builds `sourcetrail_rust_indexer`
      and installs it alongside the C++ indexer.
- [x] Add `BUILD_RUST_LANGUAGE_PACKAGE` CMake option.
- [x] Gate the Rust build behind that option in `CMakeLists.txt`.

### Recommended CMake integration: `corrosion`

```cmake
FetchContent_Declare(
    corrosion
    GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
    GIT_TAG        v0.5
    GIT_SUBMODULES ""
)
FetchContent_MakeAvailable(corrosion)
corrosion_import_crate(MANIFEST_PATH src/rust_indexer/Cargo.toml)
```

---

## Phase 5 — App-side dispatch

**Goal:** teach the C++ app to launch `sourcetrail_rust_indexer` for `.rs`
files.

### Phase 5 Tasks

- [x] Add `IndexerCommandType::Rust` handling in
      `IpcInterprocessIndexerCommandManager` / `IndexerCommandSerializer`.
      C++ indexers skip Rust commands via `popIndexerCommand(skipType)`.
- [x] Add `LanguagePackageRust` + `SourceGroupRust` + `SourceGroupFactoryModuleRust`:
  - `.rs` default source extension via `getDefaultSourceExtensions()`
  - produces `IndexerCommandRust` objects
  - `TaskBuildIndex` spawns `sourcetrail_rust_indexer` as a subprocess
- [x] Register `LanguagePackageRust` in `src/app/main.cpp` behind
      `#if BUILD_RUST_LANGUAGE_PACKAGE`.
- [x] Add `SourceGroupSettingsRustEmpty` project settings (source paths,
      exclude filters, source extensions) wired into `ProjectSettings`.

---

## Phase 6 — Testing & CI

- [x] Unit tests for FlatBuffers round-trip (Rust ↔ C++) — `ipc/storage_tests.rs`.
- [x] Unit tests for the parser — `parser/tests.rs` (73 tests as of 2026-07-10:
      all symbol kinds, semantic disambiguation, reference occurrences with
      exact line/col assertions, scope extents, type-system edges, storage
      invariants, error handling).
- [ ] Integration test: index a small Rust crate, verify the resulting
      Sourcetrail database contains expected nodes/edges.
- [x] Add `cargo test` step to `.github/workflows/cmake-multi-platform.yml`
      (ubuntu/macos/windows matrix + C++ build with full ctest suite).

---

## Phase 7 — Semantic fidelity (complete, 2026-07-10)

Commits `e311fd05..ea9f27d6` upgraded the parser from name-based heuristics
to rust-analyzer's semantic layer:

- [x] Dependency bump `ra_ap_* 0.0.321 → 0.0.341` (clears the
      `ra_ap_hir_def` future-incompatibility lint; `span::FileId` is now
      `vfs::FileId`, `parse_errors` moved onto `EditionedFileId`).
- [x] Semantic edge resolution: definitions register under HIR identity
      (`DefKey` → node id); references resolve via `Semantics`
      (`resolve_path` / `resolve_method_call` / `resolve_field`) inside
      `hir::attach_db` (next-trait-solver TLS). Name-suffix lookup remains
      only as per-site fallback (unexpanded macros).
- [x] Reference occurrences at usage sites, attached to the edge id —
      calls, value refs, field access, record literals, type refs.
- [x] `LOCATION_SCOPE` extents for definitions (snippet display).
- [x] Type-system edges: bounds from param nodes, lifetime outlives lattice,
      `EDGE_TYPE_ARGUMENT`, `EDGE_OVERRIDE` —
      see `context/DESIGN_RUST_TYPE_SYSTEM_EDGES.md`.
- [x] Implicit generic-specialization nodes (`Base<Arg>` bubbles, §7):
      `EDGE_TEMPLATE_SPECIALIZATION` + retargeted `EDGE_TYPE_ARGUMENT`,
      configurable scope `rust_specialization_scope` (off/local/all,
      default local).
- [x] `EDGE_IMPORT` for `use` items; local symbols (function-local bindings);
      `EDGE_MACRO_USAGE` for bang, derive (`#[derive(..)]`) and attribute
      (`#[my_attr]`) macro invocations. External macros (std/derive builtins,
      external proc-macros) are recorded as `NODE_MACRO` nodes with
      `DefinitionKind::NONE` and linked — mirroring how the C++ indexer
      records referenced-but-external symbols (`recordSymbol` + NONE). No
      scope knob: unlike specialization nodes (which explode per
      instantiation), macros are few and deduped, so — per Sourcetrail's own
      precedent — external ones are always recorded and the GUI filters.

Remaining gaps: none of the deferred per-symbol items remain. Larger
follow-ups: multi-subprocess fan-out across source groups (Phase 8, 3rd
bullet). (Proc-macro expansion shipped — see
`ROADMAP_PROC_MACRO_EXPANSION.md`.)

---

## Phase 8 — Parallel indexing (planned)

The pipeline is single-threaded on all three layers today; CPU sits idle
during large-crate indexing. In expected-payoff order:

- [x] **Parallel semantic reference pass via salsa snapshots.** (2026-07-10:
      RefResolver + ReferenceRow pure/apply split; scoped-thread fan-out with
      per-worker db clone + attach_db + Semantics; single-threaded ordered
      assembly; RUST_INDEXER_SERIAL=1 escape hatch. Output byte-identical to
      serial. Pass itself 1.27x on the self-index — analysis-bound crates
      scale further; load/cargo-check dominate small ones.) Pass 2
      (`collect_semantic_edges`) walks files independently — fan out with one
      `db.snapshot()` + `Semantics` per worker (the same pattern as
      rust-analyzer's `parallel_prime_caches`), resolving references per file
      concurrently and sending resolved rows to a single-threaded assembler
      that owns id allocation, `emitted_edges` dedup, and storage row pushes.
      Pass 1 (definition emission) stays serial — it populates `def_ids`,
      which pass 2 only reads, so the barrier between the passes is already
      in the right place. Determinism: assemble in file-discovery order
      (collect per-file row batches, then append in `local_files` order).
- [x] `num_worker_threads` (min(cores, 8)) and `proc_macro_processes` (2)
      raised in `LoadProfile::FULL`.
- [ ] **Multiple rust indexer subprocesses** for multi-source-group projects:
      `TaskBuildIndex` spawns exactly one Rust process (C++ gets N); K rust
      supervisor threads would parallelize across crates/source groups.
      Memory trade-off: each process loads its own workspace.
      The full per-source-group fan-out design (clusters + concurrent Turso as
      sole writer) is written up in
      [DESIGN_MULTIGROUP_FANOUT.md](DESIGN_MULTIGROUP_FANOUT.md); this Rust
      bullet is its language-specific slice.

Measure first: for small/medium crates the wall time is dominated by
workspace resolution + `cargo check` + sysroot loading (I/O and subprocess
bound); the parallel reference pass pays off where analysis dominates —
big crates. `time cargo run --release --bin index_self` is the baseline
harness (~8s as of 2026-07-10, FULL profile).

---

## Open Questions

1. **Cross-crate resolution** — ✅ resolved: indexer operates per-crate via
   `index_crate(working_directory)`. The C++ app sends one command per crate
   root (`working_directory` = directory containing `Cargo.toml`).
2. **Cargo metadata** — ✅ resolved: `ra_ap_load_cargo::load_workspace_at()`
   runs `cargo metadata` internally via `ra_ap_project_model::CargoConfig`.
3. **Proc-macro expansion** — deferred to v2. Currently using
   `ProcMacroServerChoice::None`; proc-macro bodies are skipped but their
   definitions are still indexed.
4. **`libipc` crate location** — resolved: path dependency on the vendored
   submodule `submodules/thoth-ipc/rust/libipc` (package `thoth-ipc`);
   the earlier git-dependency setup was replaced when thoth-ipc became a
   submodule.

---

## Dependencies Summary

All `ra_ap_*` crates are pinned to one version (`0.0.341` as of 2026-07-10);
they must be bumped in lockstep.

| Crate | Purpose |
| --- | --- |
| `flatbuffers` | FlatBuffers Rust runtime |
| `libipc` (path dep, `submodules/thoth-ipc/rust/libipc`) | thoth-ipc SHM channels |
| `ra_ap_syntax` | Rust CST (`ast`, `SyntaxKind`, `TextRange`) |
| `ra_ap_hir` | HIR walk + `Semantics` resolution, `GenericDef`, `attach_db` |
| `ra_ap_ide_db` | `RootDatabase`, `LineIndex`, `SourceDatabase` |
| `ra_ap_project_model` | `CargoConfig` (drives `cargo metadata`) |
| `ra_ap_load-cargo` | `load_workspace_at()` — loads full crate graph into DB |
| `ra_ap_vfs` | `Vfs` + `FileId` — maps file IDs to paths |
| `either` | `Either` in rust-analyzer public APIs (fields, param sources) |
| `tempfile` | Temp crate creation in `index_file()` shim |
| `log` + `env_logger` | Logging (mirrors C++ `LOG_INFO` macros) |
