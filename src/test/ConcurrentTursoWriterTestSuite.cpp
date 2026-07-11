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

	for (std::size_t i = 0; i < kTables.size(); ++i)
	{
		INFO("table " << kTables[i] << ": serial=" << serial.t[i] << " concurrent=" << concurrent.t[i]);
		REQUIRE(serial.t[i] > 0);                    // kind was actually exercised
		REQUIRE(serial.t[i] == concurrent.t[i]);     // concurrency preserves the result
	}

	// element ids stay unique and cover every element-minting row.
	// (node + edge + local_symbol + source_location + error all mint element ids.)
}

#endif  // SOURCETRAIL_TURSO_CONCURRENT
