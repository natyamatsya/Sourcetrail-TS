# Roadmap: Migrating from boost::interprocess to cpp-ipc + FlatBuffers

## Executive Summary

This document outlines an incremental strategy for replacing `boost::interprocess` with
[cpp-ipc](https://github.com/natyamatsya/cpp-ipc) (libipc) and FlatBuffers for structured data
exchange. The migration runs **in parallel** with the existing boost implementation so that
each phase can be validated independently before the old code is removed.

## Current Architecture Analysis

### boost::interprocess Usage

**Core abstraction** — `SharedMemory` wraps boost's managed shared memory:

```
src/lib/utility/interprocess/
├── SharedMemory.h/cpp                    (managed segment + named mutex + ScopedAccess)
├── SharedMemoryGarbageCollector.h/cpp    (cross-instance cleanup via shared Map<String,String>)
```

**Data managers** — push/pop structured data through shared memory:

```
src/lib/data/indexer/interprocess/
├── BaseInterprocessDataManager.h/cpp     (owns SharedMemory, stores processId + uuid)
├── InterprocessIndexerCommandManager     (push/pop IndexerCommand via SharedMemory::Queue)
├── InterprocessIndexingStatusManager     (indexing state: current/crashed files, interrupts)
├── InterprocessIntermediateStorageManager (push/pop IntermediateStorage via SharedMemory::Queue)
└── shared_types/
    ├── SharedStorageTypes.h              (toShared/fromShared + SharedMemory::String structs)
    ├── SharedIndexerCommand.h/cpp        (SharedMemory::String/Vector members)
    └── SharedIntermediateStorage.h/cpp   (9 SharedMemory::Vector<T> members)
```

**Other boost usage** (non-interprocess, to be cleaned up separately):

```
src/lib/utility/Platform.h/cpp           (boost::predef macros — trivially replaceable)
src/test/Catch2.cpp                       (boost predef for test configuration)
```

### Features Used from boost::interprocess

| Feature | Files | Complexity |
|---|---|---|
| `managed_shared_memory` (create/open/grow/shrink, `find_or_construct<T>`) | SharedMemory.cpp | High |
| `named_mutex` + `scoped_lock` + `try_to_lock` + `accept_ownership` | SharedMemory.cpp | Medium |
| Shared-memory allocator (`segment_manager`) | All shared_types, GarbageCollector | High |
| Shared-memory containers (`String`, `Vector`, `Queue`, `Map`, `Set`) | 9 files, ~94 usages | High |
| `shared_memory_object::remove`, `named_mutex::remove` | SharedMemory.cpp | Low |
| `permissions::set_unrestricted` | SharedMemory.cpp | Low |
| `interprocess_exception` | SharedMemory.cpp, GarbageCollector | Low |

### IPC Data Flow

```
┌─────────────────┐    shared memory     ┌──────────────────┐
│  Sourcetrail     │ ──────────────────► │  Sourcetrail      │
│  (main process)  │  IndexerCommands    │  _indexer process  │
│                  │ ◄────────────────── │                    │
│                  │  IntermStorage +     │                    │
│                  │  IndexingStatus      │                    │
└─────────────────┘                      └──────────────────┘
```

The main process pushes `IndexerCommand` objects into a shared queue. Each indexer process
pops commands, indexes source files, and pushes `IntermediateStorage` results back. Status
(current file, crashes, interrupts) is exchanged via a separate shared memory segment.

## cpp-ipc Capabilities

| Feature | cpp-ipc | Notes |
|---|---|---|
| Raw shared memory | `ipc::shm::handle` | create/open modes, ref counting |
| Named mutex | `ipc::sync::mutex` | lock/try_lock/unlock |
| Cleanup | `ipc::shm::remove`, `mutex::clear_storage` | By name |
| Message channels | `ipc::channel` | Multi-reader/writer, lock-free circular buffer |
| FlatBuffers (proto layer) | `libipc/proto/` | Typed protocol, service registry |
| macOS support | ✅ | Fork's primary addition |

**Key advantage**: FlatBuffers provide zero-copy structured reads directly from shared memory,
eliminating the need for boost's managed segment allocator and shared-memory containers entirely.

## Migration Strategy

### Guiding Principles

1. **Parallel operation** — new code runs alongside boost; a compile-time or runtime flag selects the backend.
2. **Bottom-up** — replace low-level primitives first, then data types, then managers.
3. **Schema-first** — define FlatBuffer schemas before writing transport code.
4. **Test parity** — each phase must pass all existing `SharedMemoryTestSuite` tests plus new ones.

### Phase 0: Prerequisites (no functional change)

**Goal**: Add cpp-ipc as a dependency, replace `Platform.h` boost::predef usage, prepare the build.

**Tasks**:
- [ ] Add cpp-ipc to `vcpkg.json` and `CMakeLists.txt` (as `External_lib_ipc`).
- [ ] Add FlatBuffers to `vcpkg.json` and `CMakeLists.txt` (as `External_lib_flatbuffers`).
- [ ] Replace `boost::predef` in `Platform.h/cpp` with standard `#ifdef` checks
      (`__linux__`, `_WIN32`, `__APPLE__`, `__FreeBSD__`, `sizeof(void*)`).
- [ ] Replace `boost::predef` in `src/test/Catch2.cpp` similarly.
- [ ] Verify build and all tests pass.

**Files**: `vcpkg.json`, `CMakeLists.txt`, `src/lib/CMakeLists.txt`, `Platform.h/cpp`, `Catch2.cpp`

### Phase 1: FlatBuffer Schemas

**Goal**: Define `.fbs` schemas that mirror the current shared data types.

**Tasks**:
- [ ] Create `src/lib/data/indexer/interprocess/schemas/` directory.
- [ ] Define `indexer_command.fbs`:
  ```flatbuffers
  table IndexerCommand {
    type: ubyte;
    source_file_path: string;
    indexed_paths: [string];
    exclude_filters: [string];
    include_filters: [string];
    working_directory: string;
    compiler_flags: [string];
  }
  table IndexerCommandQueue {
    commands: [IndexerCommand];
  }
  ```
- [ ] Define `intermediate_storage.fbs`:
  ```flatbuffers
  table StorageNode    { id: uint64; type: int32; serialized_name: string; }
  table StorageFile    { id: uint64; file_path: string; language: string; indexed: bool; complete: bool; }
  table StorageEdge    { id: uint64; type: int32; source_id: uint64; target_id: uint64; }
  table StorageSymbol  { id: uint64; definition_kind: int32; }
  table StorageSourceLocation { id: uint64; file_id: uint64; start_line: uint32; start_col: uint16; end_line: uint32; end_col: uint16; type: int32; }
  table StorageLocalSymbol    { id: uint64; name: string; }
  table StorageOccurrence     { element_id: uint64; source_location_id: uint64; }
  table StorageComponentAccess { node_id: uint64; type: int32; }
  table StorageError   { id: uint64; message: string; translation_unit: string; fatal: bool; indexed: bool; }

  table IntermediateStorage {
    next_id: uint64;
    nodes: [StorageNode];
    files: [StorageFile];
    edges: [StorageEdge];
    symbols: [StorageSymbol];
    source_locations: [StorageSourceLocation];
    local_symbols: [StorageLocalSymbol];
    occurrences: [StorageOccurrence];
    component_accesses: [StorageComponentAccess];
    errors: [StorageError];
  }
  table IntermediateStorageQueue {
    storages: [IntermediateStorage];
  }
  ```
- [ ] Define `indexing_status.fbs`:
  ```flatbuffers
  table IndexingStatus {
    indexing_file_paths: [string];
    current_file_paths: [string];
    crashed_file_paths: [string];
    finished_process_ids: [uint64];
    indexing_interrupted: bool;
  }
  ```
- [ ] Define `garbage_collector.fbs`:
  ```flatbuffers
  table GarbageCollectorEntry { name: string; timestamp: string; }
  table GarbageCollectorState {
    instances: [GarbageCollectorEntry];
    memory_timestamps: [GarbageCollectorEntry];
  }
  ```
- [ ] Add CMake rules to compile `.fbs` → generated headers via `flatc`.
- [ ] Write unit tests that serialize/deserialize each schema with representative data.

**Files**: New `.fbs` files, `CMakeLists.txt` updates, new test file.

### Phase 2: IpcSharedMemory — New Transport Layer

**Goal**: Create a new `IpcSharedMemory` class wrapping cpp-ipc primitives, parallel to `SharedMemory`.

**Tasks**:
- [ ] Create `src/lib/utility/interprocess/IpcSharedMemory.h/cpp`:
  - Wraps `ipc::shm::handle` for raw shared memory.
  - Wraps `ipc::sync::mutex` for named mutex with RAII `ScopedAccess`.
  - `ScopedAccess` provides `void* data()` and `size_t size()` (raw access, no managed segment).
  - Provides `write(const uint8_t* buf, size_t len)` and `const uint8_t* read(size_t* len)`.
  - Supports create/open/open_or_create modes.
  - Handles cleanup by name via `ipc::shm::remove` / `ipc::sync::mutex::clear_storage`.
- [ ] Write `IpcSharedMemoryTestSuite.cpp` covering:
  - Create + open from second handle.
  - Write/read round-trip with FlatBuffer payloads.
  - Mutex contention (two threads simulating two processes).
  - Cleanup semantics.
- [ ] Verify cpp-ipc works on all target platforms (macOS, Linux, Windows).

**Files**: New `IpcSharedMemory.h/cpp`, new test file.

### Phase 3: FlatBuffer Serializers

**Goal**: Implement conversion functions between existing C++ types and FlatBuffer representations.

**Tasks**:
- [ ] Create `src/lib/data/indexer/interprocess/serialization/` directory.
- [ ] Implement `IndexerCommandSerializer.h/cpp`:
  - `flatbuffers::DetachedBuffer serialize(const std::vector<std::shared_ptr<IndexerCommand>>&)`
  - `std::vector<std::shared_ptr<IndexerCommand>> deserialize(const uint8_t* buf, size_t len)`
- [ ] Implement `IntermediateStorageSerializer.h/cpp`:
  - `flatbuffers::DetachedBuffer serialize(const IntermediateStorage&)`
  - `std::shared_ptr<IntermediateStorage> deserialize(const uint8_t* buf, size_t len)`
- [ ] Implement `IndexingStatusSerializer.h/cpp`.
- [ ] Implement `GarbageCollectorSerializer.h/cpp`.
- [ ] Write round-trip unit tests for each serializer using real data from existing tests.

**Files**: New serializer files, new test files.

### Phase 4: New Interprocess Managers

**Goal**: Create parallel manager implementations that use `IpcSharedMemory` + FlatBuffer serializers.

**Tasks**:
- [ ] Create `IpcInterprocessIndexerCommandManager` — same interface as current, backed by
      `IpcSharedMemory` + `IndexerCommandSerializer`.
- [ ] Create `IpcInterprocessIntermediateStorageManager`.
- [ ] Create `IpcInterprocessIndexingStatusManager`.
- [ ] Create `IpcSharedMemoryGarbageCollector`.
- [ ] Add a compile-time flag (`USE_CPP_IPC`) in CMake that selects between old and new managers
      via a factory or `#ifdef` in `BaseInterprocessDataManager`.
- [ ] Write integration tests that run with the new backend.

**Files**: New manager implementations, CMake flag, factory/selection logic.

### Phase 5: Integration Testing & Switchover

**Goal**: Validate the new backend end-to-end with real indexing workloads.

**Tasks**:
- [ ] Run full indexing of the Sourcetrail tutorial project with `USE_CPP_IPC=ON`.
- [ ] Run full indexing of a large C++ project (e.g. LLVM headers) with both backends, compare results.
- [ ] Profile shared memory usage and IPC throughput.
- [ ] Fix any issues found.
- [ ] Make `USE_CPP_IPC=ON` the default.

### Phase 6: Cleanup

**Goal**: Remove boost::interprocess entirely.

**Tasks**:
- [ ] Remove `SharedMemory.h/cpp` (old boost wrapper).
- [ ] Remove `SharedMemoryGarbageCollector.h/cpp` (old boost version).
- [ ] Remove `shared_types/SharedStorageTypes.h`, `SharedIndexerCommand.h/cpp`,
      `SharedIntermediateStorage.h/cpp` (boost-allocator types).
- [ ] Remove old `BaseInterprocessDataManager` and boost-backed managers.
- [ ] Remove `boost-interprocess` from `vcpkg.json`.
- [ ] If no other boost usage remains, remove boost entirely from `CMakeLists.txt` and `vcpkg.json`.
- [ ] Remove `External_lib_boost` target.
- [ ] Rename `IpcSharedMemory` → `SharedMemory`, `Ipc*Manager` → original names.
- [ ] Update `SharedMemoryTestSuite.cpp` to only test the new backend.

## File Impact Summary

| Phase | New files | Modified files | Deleted files |
|---|---|---|---|
| 0 | — | 5 | — |
| 1 | ~5 `.fbs` + generated headers | 2 CMake | — |
| 2 | 2 (IpcSharedMemory + test) | 1 CMake | — |
| 3 | ~8 (serializers + tests) | — | — |
| 4 | ~5 (managers + factory) | 2-3 (CMake, InterprocessIndexer) | — |
| 5 | — | — | — |
| 6 | — | 3-5 (renames, CMake cleanup) | ~12 (old boost files) |

## Risk Mitigation

- **Parallel operation**: At no point is the boost backend removed before the cpp-ipc backend is
  fully validated. The `USE_CPP_IPC` flag allows instant rollback.
- **Schema evolution**: FlatBuffers schemas support forward/backward compatibility. Future
  additions to the indexer protocol won't break running indexer processes.
- **Platform coverage**: cpp-ipc's macOS support is the fork's primary addition and must be
  CI-tested before Phase 5.
- **Performance**: FlatBuffers zero-copy reads should match or exceed boost's managed segment.
  Phase 5 includes profiling to verify.

## Dependencies

- [cpp-ipc](https://github.com/natyamatsya/cpp-ipc) — shared memory + mutex + channel
- [FlatBuffers](https://github.com/google/flatbuffers) — schema-driven serialization
- Both available via vcpkg.
