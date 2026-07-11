#include "Catch2.hpp"

#include "ConcurrentStorageIndex.h"

#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Phase 4 load-bearing invariants: the concurrent id allocator + dedup maps must
// hand out one stable id per key and never collide, under many threads. (The
// standalone version of this test also passes clean under ThreadSanitizer.)

using namespace storage;

TEST_CASE("concurrent intern yields one stable id per key")
{
	constexpr int threads = 8;
	constexpr int shared = 1000;      // names interned by ALL threads (contention)
	constexpr int uniquePer = 1000;   // names unique per thread

	ConcurrentStorageIndex index;
	index.seed(1);

	struct Interned
	{
		std::string name;
		ElementId id;
		bool created;
	};
	std::vector<std::vector<Interned>> perThread(threads);

	{
		std::vector<std::thread> pool;
		for (int t = 0; t < threads; ++t)
		{
			pool.emplace_back([&, t]() {
				auto& out = perThread[t];
				for (int i = 0; i < shared; ++i)
				{
					const std::string n = "shared_" + std::to_string(i);
					const auto r = index.internNode(n, 1);
					out.push_back({n, r.id, r.created});
				}
				for (int i = 0; i < uniquePer; ++i)
				{
					const std::string n = "t" + std::to_string(t) + "_" + std::to_string(i);
					const auto r = index.internNode(n, 1);
					out.push_back({n, r.id, r.created});
				}
			});
		}
		for (auto& th : pool)
		{
			th.join();
		}
	}

	std::unordered_map<std::string, ElementId> nameToId;
	std::unordered_map<ElementId, std::string> idToName;
	std::unordered_map<std::string, int> createdCount;
	bool consistentId = true;
	bool uniqueId = true;
	for (const auto& vec : perThread)
	{
		for (const auto& e : vec)
		{
			auto [it, inserted] = nameToId.try_emplace(e.name, e.id);
			if (!inserted && it->second != e.id)
			{
				consistentId = false;
			}
			auto [rit, rinserted] = idToName.try_emplace(e.id, e.name);
			if (!rinserted && rit->second != e.name)
			{
				uniqueId = false;
			}
			if (e.created)
			{
				createdCount[e.name]++;
			}
		}
	}

	const std::size_t distinct =
		static_cast<std::size_t>(shared) + static_cast<std::size_t>(threads) * uniquePer;

	bool createdOnce = true;
	for (const auto& [name, id] : nameToId)
	{
		if (createdCount[name] != 1)
		{
			createdOnce = false;
		}
	}

	REQUIRE(consistentId);                                       // same name -> same id
	REQUIRE(uniqueId);                                           // id -> single name
	REQUIRE(createdOnce);                                        // created exactly once per name
	REQUIRE(nameToId.size() == distinct);
	REQUIRE(index.counter().peek() == static_cast<ElementId>(1 + distinct));
}

TEST_CASE("allocateRange hands out disjoint ranges under contention")
{
	constexpr int threads = 8;
	constexpr int rounds = 1000;
	constexpr std::size_t rangeLen = 7;

	AtomicIdCounter counter;
	counter.seed(1);

	std::vector<std::vector<ElementId>> bases(threads);
	{
		std::vector<std::thread> pool;
		for (int t = 0; t < threads; ++t)
		{
			pool.emplace_back([&, t]() {
				for (int r = 0; r < rounds; ++r)
				{
					bases[t].push_back(counter.allocateRange(rangeLen));
				}
			});
		}
		for (auto& th : pool)
		{
			th.join();
		}
	}

	std::unordered_map<ElementId, int> covered;
	for (const auto& vec : bases)
	{
		for (ElementId b : vec)
		{
			for (std::size_t k = 0; k < rangeLen; ++k)
			{
				covered[b + static_cast<ElementId>(k)]++;
			}
		}
	}

	bool noOverlap = true;
	for (const auto& [id, c] : covered)
	{
		if (c != 1)
		{
			noOverlap = false;
		}
	}

	const std::size_t totalIds = static_cast<std::size_t>(threads) * rounds * rangeLen;
	REQUIRE(noOverlap);
	REQUIRE(covered.size() == totalIds);
	REQUIRE(counter.peek() == static_cast<ElementId>(1 + totalIds));
}

TEST_CASE("node, edge and local-symbol ids share one collision-free id space")
{
	constexpr int threads = 8;
	constexpr int per = 1000;

	ConcurrentStorageIndex index;
	index.seed(1);

	std::vector<std::vector<ElementId>> ids(threads);
	{
		std::vector<std::thread> pool;
		for (int t = 0; t < threads; ++t)
		{
			pool.emplace_back([&, t]() {
				for (int i = 0; i < per; ++i)
				{
					ids[t].push_back(index.internNode("n" + std::to_string(t) + "_" + std::to_string(i), 1).id);
					ids[t].push_back(index.internLocalSymbol("l" + std::to_string(t) + "_" + std::to_string(i)).id);
					ids[t].push_back(index.internEdge(1, t, i).id);
				}
			});
		}
		for (auto& th : pool)
		{
			th.join();
		}
	}

	std::unordered_map<ElementId, int> seen;
	for (const auto& v : ids)
	{
		for (ElementId id : v)
		{
			seen[id]++;
		}
	}
	bool allUnique = true;
	for (const auto& [id, c] : seen)
	{
		if (c != 1)
		{
			allUnique = false;
		}
	}
	REQUIRE(allUnique);
}

TEST_CASE("concurrent node index tracks the max type per node")
{
	constexpr int threads = 8;
	constexpr int nodes = 500;

	AtomicIdCounter counter;
	ConcurrentNodeIndex nodeIndex(counter);

	// Each thread interns every node with type = its own id (0..threads-1). The
	// max recorded type for each node must therefore be threads-1, regardless of
	// interleaving. (Models forward-decl-then-definition type upgrades.)
	{
		std::vector<std::thread> pool;
		for (int t = 0; t < threads; ++t)
		{
			pool.emplace_back([&, t]() {
				for (int i = 0; i < nodes; ++i)
				{
					nodeIndex.intern("n" + std::to_string(i), t);
				}
			});
		}
		for (auto& th : pool)
		{
			th.join();
		}
	}

	int count = 0;
	bool allMax = true;
	nodeIndex.forEachIdMaxType([&](ElementId /*id*/, int maxType) {
		++count;
		if (maxType != threads - 1)
		{
			allMax = false;
		}
	});
	REQUIRE(count == nodes);
	REQUIRE(allMax);
}
