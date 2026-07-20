// `srctrl.interprocess` -- the shared-memory transport between the app and its indexer
// subprocesses: IpcSharedMemory (the thoth-ipc shm+mutex+condition wrapper) and its
// cross-process garbage collector, the flatbuffers IpcSerializer stack (garbage-collector /
// indexer-command / indexing-status / intermediate-storage wire forms) with the type-erased
// IndexerCommandCodec registry, the three Ipc managers (command queue, status board,
// intermediate-storage queue), the IntermediateStorageChunker that keeps payloads below the
// fixed segment size (ADR-0002), and InterprocessIndexer -- the subprocess work loop.
// A standalone module ABOVE srctrl.indexer and srctrl.storage (it serializes IndexerCommands
// and IntermediateStorages and drives the language packages). Module build only.
//
// GMF hazards this wrapper exists to contain: the four flatbuffers-generated wire headers are
// non-modularized generated code -- global module only. The thoth-ipc primitives come in via
// `import thoth.ipc;` (upstream's opt-in named module) rather than GMF includes.
//
// Deliberately NOT here: TaskBuildIndex / TaskFillIndexerCommandQueue (host-side tasks that drag
// messaging and scheduling -- they stay classic until messaging modularizes), and
// InterprocessBackend.h's per-language registration glue in Application.cpp.

module;

#ifndef SRCTRL_IMPORT_STD
#include <algorithm>
#include <atomic>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#endif

// flatbuffers + the generated wire-format headers (build-dir generated, non-modularized).
#include <flatbuffers/flatbuffers.h>
#include "garbage_collector_generated.h"
#include "indexer_command_generated.h"
#include "indexing_status_generated.h"
#include "intermediate_storage_generated.h"

// Non-modularized GMF deps: Id/types, the logging macros, the generated language-package flags,
// utilityExpected (std-only expected helpers), and ScopedFunctor (std-only classic header).
#include "types.h"
#include "logging.h"
#include "language_package_flags.h"
#include "utilityExpected.h"
#include "ScopedFunctor.h"

export module srctrl.interprocess;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import thoth.ipc;        // the shared-memory / named-mutex / condition primitives
                         // (IpcSharedMemory's members) -- upstream's sqlpp23-style re-export
                         // module, same global-module entities the classic headers declare
import srctrl.utility;   // utilityEnum (MAX_ENUM_VALUE, enum to_string/streaming), TimeStamp
import srctrl.file;      // FilePath, FileRegister
import srctrl.data;      // the storage-row enums (NodeKind, Edge::EdgeType, LocationType, ...)
import srctrl.storage;   // IntermediateStorage + the Storage* row types
import srctrl.indexer;   // IndexerCommand(+Type, Rust/Swift/Zig payloads), IndexerBase,
                         // IndexerComposite, LanguagePackageManager
import srctrl.logging;   // srctrl::log machinery behind the LOG_* macros

#define SRCTRL_MODULE_PURVIEW
// Class definitions in dependency order (IpcSharedMemory before everything that embeds it;
// serializers before the managers and the GC that call them), then the inline bodies (each
// header's own bottom-include is purview-guarded).
#include "ProcessId.h"
#include "IpcSharedMemory.h"
#include "GarbageCollectorSerializer.h"
#include "IpcSharedMemoryGarbageCollector.h"
#include "IndexerCommandCodec.h"
#include "IndexerCommandCodecRegistry.h"
#include "IndexerCommandSerializer.h"
#include "IndexingStatusSerializer.h"
#include "IntermediateStorageSerializer.h"
#include "IntermediateStorageChunker.h"
#include "IpcInterprocessIndexerCommandManager.h"
#include "IpcInterprocessIndexingStatusManager.h"
#include "IpcInterprocessIntermediateStorageManager.h"
#include "InterprocessBackend.h"
#include "InterprocessIndexer.h"

#include "IpcSharedMemory.inl"
#include "GarbageCollectorSerializer.inl"
#include "IpcSharedMemoryGarbageCollector.inl"
#include "IndexerCommandCodecRegistry.inl"
#include "IndexerCommandSerializer.inl"
#include "IndexingStatusSerializer.inl"
#include "IntermediateStorageSerializer.inl"
#include "IntermediateStorageChunker.inl"
#include "IpcInterprocessIndexerCommandManager.inl"
#include "IpcInterprocessIndexingStatusManager.inl"
#include "IpcInterprocessIntermediateStorageManager.inl"
#include "InterprocessIndexer.inl"
