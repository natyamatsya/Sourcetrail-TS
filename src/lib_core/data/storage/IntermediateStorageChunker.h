#ifndef INTERMEDIATE_STORAGE_CHUNKER_H
#define INTERMEDIATE_STORAGE_CHUNKER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <memory>
#include <vector>

class IntermediateStorage;
#endif

namespace utility
{
// Splits an IntermediateStorage into self-contained chunks that each serialize
// well below the fixed shared-memory segment size, so the interprocess queue
// never needs to grow (docs/adr/ADR-0002-no-shm-growth.md). Mirrors the Rust
// indexer's OwnedIntermediateStorage::chunks()
// (src/rust_indexer/indexer/src/ipc/storage.rs).
//
// Each chunk is self-contained because inject remaps ids per injected storage:
// edges carry their endpoint node rows, source locations their file node
// (+ file) rows, occurrences their element and location rows. Repeated rows
// merge on inject (nodes by serialized name, edges by (type, source, target),
// files by path, locations by position). Occurrences and symbols are not
// deduped on inject, so each is emitted in exactly one chunk.
//
// A storage that fits the budget is returned unchanged (same instance).
SRCTRL_EXPORT std::vector<std::shared_ptr<IntermediateStorage>> chunkIntermediateStorage(
	const std::shared_ptr<IntermediateStorage>& storage);
}	 // namespace utility

#ifndef SRCTRL_MODULE_PURVIEW
#include "IntermediateStorageChunker.inl"
#endif

#endif	  // INTERMEDIATE_STORAGE_CHUNKER_H
