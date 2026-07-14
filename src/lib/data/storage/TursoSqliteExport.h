#ifndef TURSO_SQLITE_EXPORT_H
#define TURSO_SQLITE_EXPORT_H

#ifdef SOURCETRAIL_TURSO_CONCURRENT

class ConcurrentTursoWriter;
class StorageConnection;

//! Fan-out S4 ("export, don't swap"): single-threaded copy of the drained
//! concurrent-Turso ingest database into the SQLite index storage behind
//! `connection`, preserving ids verbatim. The target graph tables are expected
//! to be empty (full refresh): all dedup and id assignment already happened in
//! the writer's in-process index, so this is a plain bulk INSERT per table
//! inside one transaction. The SQLite read/meta/bookmark/swap machinery stays
//! untouched — the Turso writer is purely an ingest accelerator.
//! Call only after ConcurrentTursoWriter::finish(). Returns false (and rolls
//! back) on failure.
bool exportConcurrentTursoToSqlite(
	const ConcurrentTursoWriter& writer, StorageConnection& connection);

#endif	  // SOURCETRAIL_TURSO_CONCURRENT
#endif	  // TURSO_SQLITE_EXPORT_H
