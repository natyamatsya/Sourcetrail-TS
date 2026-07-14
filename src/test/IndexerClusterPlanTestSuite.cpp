#include "Catch2.hpp"

#include "IndexerClusterPlan.h"

namespace
{
IndexerClusterEntry cluster(const std::string& groupId, size_t commandCount)
{
	return IndexerClusterEntry{groupId, LanguageType::CXX, commandCount, 0};
}

std::vector<size_t> allocate(const std::vector<IndexerClusterEntry>& clusters, size_t budget)
{
	std::vector<size_t> counts;
	for (const IndexerClusterEntry& entry: allocateIndexerSubprocesses(clusters, budget))
	{
		counts.push_back(entry.subprocessCount);
	}
	return counts;
}
}  // namespace

TEST_CASE("indexer cluster plan: subprocess allocation")
{
	SECTION("proportional split with a minimum of one per non-empty cluster")
	{
		// 12 slots over 900/90/10: min-1 each, the rest by largest remainder.
		REQUIRE(allocate({cluster("a", 900), cluster("b", 90), cluster("c", 10)}, 12) ==
			std::vector<size_t>{9, 2, 1});
	}

	SECTION("skewed two-group split")
	{
		REQUIRE(allocate({cluster("big", 1000), cluster("small", 50)}, 8) ==
			std::vector<size_t>{7, 1});
	}

	SECTION("budget smaller than cluster count still yields one each")
	{
		REQUIRE(allocate({cluster("a", 5), cluster("b", 5), cluster("c", 5)}, 2) ==
			std::vector<size_t>{1, 1, 1});
	}

	SECTION("clusters without commands get no subprocess")
	{
		REQUIRE(allocate({cluster("a", 10), cluster("empty", 0), cluster("b", 10)}, 4) ==
			std::vector<size_t>{2, 0, 2});
	}

	SECTION("a cluster never gets more subprocesses than commands")
	{
		// 3+3 commands cannot use 8 slots; the surplus stays unassigned.
		REQUIRE(allocate({cluster("a", 3), cluster("b", 3)}, 8) ==
			std::vector<size_t>{3, 3});
	}

	SECTION("single cluster takes the whole budget")
	{
		REQUIRE(allocate({cluster("only", 100)}, 5) == std::vector<size_t>{5});
	}

	SECTION("remainder ties resolve deterministically to earlier clusters")
	{
		REQUIRE(allocate({cluster("a", 10), cluster("b", 10)}, 3) ==
			std::vector<size_t>{2, 1});
	}

	SECTION("all clusters empty yields no subprocesses")
	{
		REQUIRE(allocate({cluster("a", 0), cluster("b", 0)}, 4) ==
			std::vector<size_t>{0, 0});
	}
}
