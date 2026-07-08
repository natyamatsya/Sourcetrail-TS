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

- `src/lib/data/indexer/interprocess/schemas/indexer_command.fbs`
  — `IndexerCommandQueue` / `IndexerCommand` / `IndexerCommandType`
- `src/lib/data/indexer/interprocess/schemas/intermediate_storage.fbs`
  — `IntermediateStorageQueue` / `IntermediateStorage` and all sub-tables
- `src/lib/data/indexer/interprocess/schemas/indexing_status.fbs`
  — `IndexingStatus` / `ProcessFile`
- `src/lib/data/indexer/interprocess/schemas/garbage_collector.fbs`
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
flatbuffers = "24"   # matches the vcpkg flatbuffers version in use
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

### Symbol mapping to Sourcetrail node/edge kinds

| Rust construct | `NodeKind` | `EdgeType` |
| --- | --- | --- |
| `mod` | `NODE_MODULE` | — |
| `struct` / `enum` / `union` | `NODE_STRUCT` / `NODE_ENUM` / `NODE_UNION` | — |
| `trait` | `NODE_INTERFACE` | — |
| `impl Trait for Type` | — | `EDGE_INHERITANCE` |
| `fn` (free / method) | `NODE_FUNCTION` / `NODE_METHOD` | — |
| `use` item | — | `EDGE_IMPORT` |
| function call | — | `EDGE_CALL` |
| field access | — | `EDGE_MEMBER` |
| type reference | — | `EDGE_TYPE_USAGE` |
| `const` / `static` | `NODE_GLOBAL_VARIABLE` | — |
| `type` alias | `NODE_TYPEDEF` | — |
| macro invocation | `NODE_MACRO` | `EDGE_MACRO_USAGE` |

### Phase 3 Tasks

- [x] Implement `RustIndexer::index(command) -> IntermediateStorage` using
      `syn` for a prototype (single-file, no cross-file resolution).
- [x] Wire up source locations (`start_line`, `start_col`, `end_line`,
      `end_col`) from `proc_macro2::Span` (with `span-locations` feature).
- [x] Upgrade to `ra_ap_*` for cross-file resolution (Phase 3b — complete).
      `load_workspace_at()` + `Module::declarations()` + `Module::legacy_macros()`
      + `RootQueryDb::parse_errors()`. Self-indexing verified: 11 files, 64 nodes,
      0 errors. `index_crate(working_directory)` used in production binary.
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

- [x] Unit tests for FlatBuffers round-trip (Rust ↔ C++) — `ipc/storage_tests.rs`
      (7 tests: nodes, files, source_locations, occurrences, symbols, errors, empty).
- [x] Unit tests for each symbol kind extracted by the parser — `parser/mod.rs`
      (20 tests: all symbol kinds, module prefix, error handling, storage invariants).
- [ ] Integration test: index a small Rust crate, verify the resulting
      Sourcetrail database contains expected nodes/edges.
- [x] Add `cargo test` step to `.github/workflows/cmake-multi-platform.yml`
      (ubuntu/macos/windows matrix + C++ smoke build).

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
4. **`libipc` crate location** — resolved: fetched as a git dependency from
   `github.com/natyamatsya/thoth-ipc` (rev `32b772d`), no local path dep.

---

## Dependencies Summary

| Crate | Purpose |
| --- | --- |
| `flatbuffers` | FlatBuffers Rust runtime |
| `libipc` (git dep, `natyamatsya/thoth-ipc`) | thoth-ipc SHM channels |
| `ra_ap_syntax` | Rust CST |
| `ra_ap_hir` | Name resolution, HIR walk (`ModuleDef`, `HasSource`, `AsAssocItem`) |
| `ra_ap_ide_db` | `RootDatabase`, `LineIndex`, `SourceDatabase`, `RootQueryDb` |
| `ra_ap_project_model` | `CargoConfig` (drives `cargo metadata`) |
| `ra_ap_load-cargo` | `load_workspace_at()` — loads full crate graph into DB |
| `ra_ap_vfs` | `Vfs` + `FileId` — maps file IDs to paths |
| `tempfile` | Temp crate creation in `index_file()` shim |
| `log` + `env_logger` | Logging (mirrors C++ `LOG_INFO` macros) |
