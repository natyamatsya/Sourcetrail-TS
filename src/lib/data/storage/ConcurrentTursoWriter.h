#ifndef CONCURRENT_TURSO_WRITER_H
#define CONCURRENT_TURSO_WRITER_H

#include <memory>
#include <string>
#include <vector>

#include "StorageComponentAccess.h"
#include "StorageEdge.h"
#include "StorageElementComponent.h"
#include "StorageError.h"
#include "StorageFile.h"
#include "StorageLocalSymbol.h"
#include "StorageNode.h"
#include "StorageOccurrence.h"
#include "StorageSourceLocation.h"
#include "StorageSymbol.h"

// Phase 4 live wiring (first slice): a Turso-only concurrent-writer inject path.
//
// N writer threads (on the lib's stdexec static_thread_pool) each own a Turso
// connection on one shared MVCC database and commit with BEGIN CONCURRENT.
// Element ids and node/edge/local-symbol dedup are coordinated in-process by a
// shared ConcurrentStorageIndex (never via MVCC-visible DB reads). Batches from
// the indexer's IntermediateStorage stream are submitted and drained before any
// read. Turso-only because concurrent writers are incompatible with the single-
// writer SQLite (and the SQLite/Turso compare) model.
//
// All 10 entity kinds are written (nodes, edges, local symbols, symbols, files,
// filecontent, source locations, occurrences, component accesses, element
// components, errors). Commits retry on transient MVCC conflicts (bounded, with
// backoff); every INSERT is OR IGNORE and ids are pre-assigned in-process, so a
// retried transaction is idempotent. Deferred: node-type upgrade on re-add.
//
// The implementation is pimpl'd so this header pulls in no Turso/stdexec headers;
// the .cpp compiles only when SOURCETRAIL_TURSO_CONCURRENT is defined.
class ConcurrentTursoWriter
{
public:
	//! One IntermediateStorage, with LOCAL ids. Edges/symbols/files/occurrences/
	//! etc. reference node (and source-location) LOCAL ids within the same batch;
	//! they are remapped to shared DB ids during processing.
	struct Batch
	{
		std::vector<StorageNode> nodes;
		std::vector<StorageEdge> edges;
		std::vector<StorageLocalSymbol> localSymbols;
		std::vector<StorageSymbol> symbols;
		std::vector<StorageFile> files;
		std::vector<StorageSourceLocation> sourceLocations;
		std::vector<StorageOccurrence> occurrences;
		std::vector<StorageComponentAccess> componentAccesses;
		std::vector<StorageElementComponent> elementComponents;
		std::vector<StorageError> errors;
	};

	//! Opens/creates the Turso database at dbPath in MVCC mode and starts
	//! `numWriters` worker threads.
	ConcurrentTursoWriter(const std::string& dbPath, int numWriters);
	~ConcurrentTursoWriter();

	ConcurrentTursoWriter(const ConcurrentTursoWriter&) = delete;
	ConcurrentTursoWriter& operator=(const ConcurrentTursoWriter&) = delete;

	//! Enqueue a batch for a worker to write. Thread-safe; returns immediately.
	void submit(Batch batch);

	//! Signal end-of-stream, wait for all writers to drain, close connections.
	//! Must be called before reading the database.
	void finish();

	//! Number of batches that failed to commit after exhausting conflict
	//! retries (0 on a clean run).
	long long failedBatches() const;

	//! Number of batches that needed at least one MVCC conflict retry.
	//! Instrumentation only — retried batches commit normally.
	long long retriedBatches() const;

	//! Inspection helper: run a scalar integer query on the database.
	long long scalar(const std::string& sql) const;

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

#endif  // CONCURRENT_TURSO_WRITER_H
