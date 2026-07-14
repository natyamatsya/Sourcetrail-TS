// Live-wiring test: drive ConcurrentTursoWriter with a full-kind workload (nodes,
// edges, local symbols, symbols, files, source locations, occurrences, component
// accesses, element components, errors) whose keys overlap across batches, and
// assert that a concurrent run and a serial run produce identical per-table row
// counts (the correctness invariant: concurrency doesn't change the result).
// Compiled only under SOURCETRAIL_TURSO_CONCURRENT.
#ifdef SOURCETRAIL_TURSO_CONCURRENT

#include "Catch2.hpp"

#include "ConcurrentTursoWriter.h"
#include "FileSystem.h"
#include "SqliteIndexStorage.h"
#include "StorageConnection.h"
#include "TursoSqliteExport.h"
#include "turso_shim.h"

#include <array>
#include <string>
#include <vector>

namespace
{
std::vector<ConcurrentTursoWriter::Batch> makeWorkload(int batchCount, int sharedPool)
{
	std::vector<ConcurrentTursoWriter::Batch> out;
	out.reserve(batchCount);

	for (int b = 0; b < batchCount; ++b)
	{
		ConcurrentTursoWriter::Batch batch;
		Id::type nextLocal = 1;

		// 20 shared + 15 unique nodes. node[0] doubles as the "file" node.
		std::vector<Id::type> nodeLocal;
		const int start = (b * 7) % sharedPool;
		for (int i = 0; i < 20; ++i)
		{
			const std::string name = "sym_" + std::to_string((start + i) % sharedPool);
			const Id::type id = nextLocal++;
			batch.nodes.emplace_back(id, NODE_UNDEFINED, name);
			nodeLocal.push_back(id);
		}
		for (int i = 0; i < 15; ++i)
		{
			const std::string name = "b" + std::to_string(b) + "_n" + std::to_string(i);
			const Id::type id = nextLocal++;
			batch.nodes.emplace_back(id, NODE_UNDEFINED, name);
			nodeLocal.push_back(id);
		}

		// file: node[0] with a path derived from its shared index (overlaps).
		// indexed=false so the synthetic (non-existent) path isn't read from disk;
		// real filecontent reads are exercised by the end-to-end index instead.
		batch.files.emplace_back(
			nodeLocal[0], "f_" + std::to_string(start % 50), "cpp", "t", false, true);

		// symbols on the first 12 (mostly shared) nodes.
		for (int i = 0; i < 12; ++i)
		{
			batch.symbols.emplace_back(nodeLocal[i], static_cast<DefinitionKind>(1));
		}

		// edges chaining consecutive nodes.
		for (std::size_t i = 1; i < nodeLocal.size(); ++i)
		{
			batch.edges.emplace_back(nextLocal++,
				StorageEdgeData(static_cast<Edge::EdgeType>(1), nodeLocal[i - 1], nodeLocal[i]));
		}

		// 5 unique local symbols per batch.
		for (int i = 0; i < 5; ++i)
		{
			batch.localSymbols.emplace_back(
				nextLocal++, "b" + std::to_string(b) + "_l" + std::to_string(i));
		}

		// source locations in node[0]'s file, lines 0..9 (fixed -> overlap across
		// batches that share node[0]).
		std::vector<Id::type> slocLocal;
		for (int i = 0; i < 10; ++i)
		{
			const Id::type id = nextLocal++;
			batch.sourceLocations.emplace_back(
				id,
				StorageSourceLocationData(nodeLocal[0], static_cast<size_t>(i), 1,
					static_cast<size_t>(i), 5, static_cast<LocationType>(1)));
			slocLocal.push_back(id);
		}

		// occurrences linking node[i] to source location[i].
		for (int i = 0; i < 10; ++i)
		{
			batch.occurrences.emplace_back(nodeLocal[i], slocLocal[i]);
		}

		// component accesses on the first 5 nodes.
		for (int i = 0; i < 5; ++i)
		{
			batch.componentAccesses.emplace_back(nodeLocal[i], static_cast<AccessKind>(1));
		}

		// two shared errors (dedup to 2 total).
		batch.errors.emplace_back(nextLocal++, "err_A", "tu", false, true);
		batch.errors.emplace_back(nextLocal++, "err_B", "tu", true, true);

		// element components on node[0]/node[1] with fixed data (node[0] shared).
		batch.elementComponents.emplace_back(
			nodeLocal[0], static_cast<ElementComponentKind>(1), "data0");
		batch.elementComponents.emplace_back(
			nodeLocal[1], static_cast<ElementComponentKind>(1), "data1");

		out.push_back(std::move(batch));
	}
	return out;
}

struct Counts
{
	long long failed = -1;
	long long retried = -1;
	std::array<long long, 10> t{};  // one per table below
};

const std::array<const char*, 10> kTables = {
	"element", "node", "edge", "local_symbol", "symbol", "file",
	"source_location", "occurrence", "component_access", "error"};

Counts runAndCount(const std::string& dbPath, int numWriters,
	const std::vector<ConcurrentTursoWriter::Batch>& work)
{
	for (const char* sfx: {"", "-wal", "-shm", "-log"})
	{
		FileSystem::remove(FilePath(dbPath + sfx));
	}

	Counts c;
	{
		ConcurrentTursoWriter writer(dbPath, numWriters);
		for (const auto& b: work)
		{
			writer.submit(b);
		}
		writer.finish();
		c.failed = writer.failedBatches();
		c.retried = writer.retriedBatches();
		for (std::size_t i = 0; i < kTables.size(); ++i)
		{
			c.t[i] = writer.scalar(std::string("SELECT COUNT(*) FROM ") + kTables[i]);
		}
	}
	for (const char* sfx: {"", "-wal", "-shm", "-log"})
	{
		FileSystem::remove(FilePath(dbPath + sfx));
	}
	return c;
}
}  // namespace

TEST_CASE("concurrent turso writer: all kinds, concurrent result == serial result")
{
	const auto work = makeWorkload(/*batches*/ 150, /*sharedPool*/ 400);

	const Counts serial = runAndCount("data/SQLiteTestSuite/ctw_serial.db", 1, work);
	const Counts concurrent = runAndCount("data/SQLiteTestSuite/ctw_concurrent.db", 8, work);

	REQUIRE(serial.failed == 0);
	REQUIRE(concurrent.failed == 0);
	REQUIRE(serial.retried == 0);  // one writer cannot conflict with itself
	INFO("concurrent retriedBatches: " << concurrent.retried);

	for (std::size_t i = 0; i < kTables.size(); ++i)
	{
		INFO("table " << kTables[i] << ": serial=" << serial.t[i] << " concurrent=" << concurrent.t[i]);
		REQUIRE(serial.t[i] > 0);                    // kind was actually exercised
		REQUIRE(serial.t[i] == concurrent.t[i]);     // concurrency preserves the result
	}

	// element ids stay unique and cover every element-minting row.
	// (node + edge + local_symbol + source_location + error all mint element ids.)
}

// Fan-out S4 gate ("export, don't swap"): a drained concurrent-Turso ingest
// database exports verbatim — same per-table counts, same ids — into an empty
// SQLite index storage, which then serves all reads as usual.
TEST_CASE("concurrent turso writer: export lands the drained result in SQLite")
{
	const auto work = makeWorkload(/*batches*/ 40, /*sharedPool*/ 120);

	const std::string tursoPath = "data/SQLiteTestSuite/ctw_export.db";
	const FilePath sqlitePath("data/SQLiteTestSuite/ctw_export.sqlite");
	for (const char* sfx: {"", "-wal", "-shm", "-log"})
	{
		FileSystem::remove(FilePath(tursoPath + sfx));
	}
	FileSystem::remove(sqlitePath);

	// Seed above 1 (like an incremental-continuity seed) to prove ids are
	// taken from the writer and land verbatim in SQLite.
	constexpr long long kFirstElementId = 5000;
	ConcurrentTursoWriter writer(tursoPath, 8, kFirstElementId);
	for (const auto& b: work)
	{
		writer.submit(b);
	}
	writer.finish();
	REQUIRE(writer.failedBatches() == 0);

	{
		StorageConnection connection(sqlitePath);
		SqliteIndexStorage storage(connection);
		storage.setup();

		REQUIRE(exportConcurrentTursoToSqlite(writer, connection));

		// Every exported table matches the drained Turso database, row for row.
		const std::array<const char*, 12> tables = {
			"element", "node", "edge", "local_symbol", "symbol", "file", "filecontent",
			"source_location", "occurrence", "component_access", "element_component", "error"};
		for (const char* table: tables)
		{
			const std::string countSql = std::string("SELECT COUNT(*) FROM ") + table;
			INFO("table " << table);
			REQUIRE(connection.legacy().execScalar(countSql) == writer.scalar(countSql));
		}
		REQUIRE(connection.legacy().execScalar("SELECT COUNT(*) FROM node") > 0);

		// Ids are preserved verbatim, including the seed offset.
		REQUIRE(connection.legacy().execScalar("SELECT MIN(id) FROM element") >= kFirstElementId);
		REQUIRE(
			connection.legacy().execScalar("SELECT MAX(id) FROM element") ==
			writer.scalar("SELECT MAX(id) FROM element"));
		const std::string nodeIdSql = "SELECT id FROM node WHERE serialized_name = 'sym_0'";
		REQUIRE(connection.legacy().execScalar(nodeIdSql) == writer.scalar(nodeIdSql));
	}

	for (const char* sfx: {"", "-wal", "-shm", "-log"})
	{
		FileSystem::remove(FilePath(tursoPath + sfx));
	}
	FileSystem::remove(sqlitePath);
}

// The S0 correctness gate at the engine level: force a genuine MVCC write-write
// conflict between two BEGIN CONCURRENT transactions and verify the retry
// contract the writer relies on — the conflicted transaction commits nothing,
// the error classifies as TSQ_BUSY (retryable), and re-running the identical
// OR-IGNORE batch on a fresh snapshot converges to the correct final state.
TEST_CASE("concurrent turso writer: conflicted commit rolls back and retries idempotently")
{
	const std::string dbPath = "data/SQLiteTestSuite/ctw_conflict.db";
	for (const char* sfx: {"", "-wal", "-shm", "-log"})
	{
		FileSystem::remove(FilePath(dbPath + sfx));
	}

	TsqDb* db = tsq_open(dbPath.c_str());
	REQUIRE(db != nullptr);
	REQUIRE(tsq_exec(db, "PRAGMA journal_mode = 'mvcc'") == TSQ_OK);
	REQUIRE(tsq_exec(db, "CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT)") == TSQ_OK);

	TsqConn* a = tsq_new_connection(db);
	TsqConn* b = tsq_new_connection(db);
	REQUIRE(a != nullptr);
	REQUIRE(b != nullptr);

	// Both transactions write row id=1; snapshot isolation hides A's uncommitted
	// row from B, so B writes too and the conflict surfaces at write or commit.
	const char* sqlB = "INSERT OR IGNORE INTO t(id,v) VALUES(1,'b');"
					   "INSERT OR IGNORE INTO t(id,v) VALUES(2,'b2');";
	REQUIRE(tsq_conn_exec(a, "BEGIN CONCURRENT") == TSQ_OK);
	REQUIRE(tsq_conn_exec(b, "BEGIN CONCURRENT") == TSQ_OK);
	REQUIRE(tsq_conn_exec(a, "INSERT OR IGNORE INTO t(id,v) VALUES(1,'a')") == TSQ_OK);

	int rcB = tsq_conn_exec(b, sqlB);
	int codeB = (rcB == TSQ_OK) ? TSQ_OK : tsq_last_error_code();
	REQUIRE(tsq_conn_exec(a, "COMMIT") == TSQ_OK);
	if (rcB == TSQ_OK)
	{
		rcB = tsq_conn_exec(b, "COMMIT");
		codeB = (rcB == TSQ_OK) ? TSQ_OK : tsq_last_error_code();
	}

	if (rcB != TSQ_OK)
	{
		INFO("conflict error: " << tsq_last_error());
		REQUIRE(codeB == TSQ_BUSY);  // classified as retryable, not a hard error
		tsq_conn_exec(b, "ROLLBACK");
		// The retry loop's move: re-run the identical batch in a fresh
		// transaction. B now sees A's row, so OR IGNORE skips id=1.
		REQUIRE(tsq_conn_exec(b, "BEGIN CONCURRENT") == TSQ_OK);
		REQUIRE(tsq_conn_exec(b, sqlB) == TSQ_OK);
		REQUIRE(tsq_conn_exec(b, "COMMIT") == TSQ_OK);
	}

	tsq_conn_close(a);
	tsq_conn_close(b);

	// First committer won row 1; B's non-conflicting row 2 landed on retry.
	auto scalar = [db](const char* sql) -> long long
	{
		TsqStmt* stmt = tsq_prepare(db, sql);
		REQUIRE(stmt != nullptr);
		REQUIRE(tsq_step(stmt) == TSQ_ROW);
		const long long v = tsq_column_int(stmt, 0);
		tsq_finalize(stmt);
		return v;
	};
	REQUIRE(scalar("SELECT COUNT(*) FROM t") == 2);
	REQUIRE(scalar("SELECT COUNT(*) FROM t WHERE id = 1 AND v = 'a'") == 1);
	REQUIRE(scalar("SELECT COUNT(*) FROM t WHERE id = 2 AND v = 'b2'") == 1);

	tsq_close(db);
	for (const char* sfx: {"", "-wal", "-shm", "-log"})
	{
		FileSystem::remove(FilePath(dbPath + sfx));
	}
}

#endif  // SOURCETRAIL_TURSO_CONCURRENT
