#include "Catch2.hpp"

#include <set>

#include "FilePath.h"
#include "ShardConfig.h"

// Distributed indexing depends on shard::stripeFilter partitioning the sorted
// translation-unit set into N disjoint, complete stripes -- deterministically,
// so independent producer processes never index the same file and never miss one.

namespace
{
std::set<FilePath> makeFileSet(int count)
{
	std::set<FilePath> files;
	for (int i = 0; i < count; i++)
	{
		files.insert(FilePath("/project/src/file" + std::to_string(i) + ".cpp"));
	}
	return files;
}
}	 // namespace

TEST_CASE("shard stripeFilter with count 1 is a no-op")
{
	std::set<FilePath> files = makeFileSet(10);
	const std::set<FilePath> original = files;
	shard::stripeFilter(&files, 1, 1);
	REQUIRE(files == original);
}

TEST_CASE("shard stripes are disjoint and complete")
{
	const int fileCount = 37;	// prime-ish, uneven across shard counts
	const std::set<FilePath> all = makeFileSet(fileCount);

	for (size_t shardCount: {size_t(2), size_t(3), size_t(5), size_t(8)})
	{
		std::set<FilePath> union_;
		size_t total = 0;
		for (size_t index = 1; index <= shardCount; index++)
		{
			std::set<FilePath> stripe = all;
			shard::stripeFilter(&stripe, index, shardCount);
			total += stripe.size();
			for (const FilePath& p: stripe)
			{
				// disjoint: no file appears in two stripes
				REQUIRE(union_.insert(p).second);
			}
		}
		// complete: every file is covered exactly once
		REQUIRE(total == static_cast<size_t>(fileCount));
		REQUIRE(union_ == all);
	}
}

TEST_CASE("shard stripeFilter is deterministic")
{
	const std::set<FilePath> all = makeFileSet(50);
	for (size_t index = 1; index <= 4; index++)
	{
		std::set<FilePath> first = all;
		std::set<FilePath> second = all;
		shard::stripeFilter(&first, index, 4);
		shard::stripeFilter(&second, index, 4);
		REQUIRE(first == second);
	}
}

TEST_CASE("shard stripes balance to within one file")
{
	const int fileCount = 100;
	const std::set<FilePath> all = makeFileSet(fileCount);
	const size_t shardCount = 7;

	size_t minSize = fileCount;
	size_t maxSize = 0;
	for (size_t index = 1; index <= shardCount; index++)
	{
		std::set<FilePath> stripe = all;
		shard::stripeFilter(&stripe, index, shardCount);
		minSize = std::min(minSize, stripe.size());
		maxSize = std::max(maxSize, stripe.size());
	}
	REQUIRE(maxSize - minSize <= 1);
}

TEST_CASE("shard ShardConfig isActive only when count exceeds one")
{
	REQUIRE_FALSE(ShardConfig().isActive());

	ShardConfig single;
	single.index = 1;
	single.count = 1;
	REQUIRE_FALSE(single.isActive());

	ShardConfig sharded;
	sharded.index = 1;
	sharded.count = 2;
	REQUIRE(sharded.isActive());
}
