#include "Catch2.hpp"

#include <set>

#ifndef SRCTRL_MODULE_BUILD
#include "FilePath.h"
#endif
#include "ShardConfig.h"

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.file;
#endif

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

// SW7: package/crate-granular striping — Rust crates and Swift packages are
// whole work units, so distributed producers must split packages, not files.

namespace
{
std::set<std::string> makePackageKeys(int count)
{
	std::set<std::string> keys;
	for (int i = 0; i < count; i++)
	{
		keys.insert("/repo/pkg" + std::to_string(i));
	}
	return keys;
}
}	 // namespace

TEST_CASE("shard stripeKeys with count 1 returns every key")
{
	const std::set<std::string> keys = makePackageKeys(6);
	REQUIRE(shard::stripeKeys(keys, 1, 1) == keys);
}

TEST_CASE("shard stripeKeys partitions packages disjointly and completely")
{
	const int packageCount = 23;	// uneven across the shard counts below
	const std::set<std::string> all = makePackageKeys(packageCount);

	for (size_t shardCount: {size_t(2), size_t(3), size_t(4), size_t(7)})
	{
		std::set<std::string> union_;
		size_t total = 0;
		for (size_t index = 1; index <= shardCount; index++)
		{
			const std::set<std::string> stripe =
				shard::stripeKeys(all, index, shardCount);
			total += stripe.size();
			union_.insert(stripe.begin(), stripe.end());
		}
		// Complete: the union is the full set. Disjoint: sizes sum to the
		// total with no key counted twice.
		REQUIRE(union_ == all);
		REQUIRE(total == all.size());
	}
}

TEST_CASE("shard stripeKeys is deterministic and balanced")
{
	const std::set<std::string> all = makePackageKeys(10);
	// Same inputs -> same stripe, every time (independent producer processes).
	REQUIRE(shard::stripeKeys(all, 2, 3) == shard::stripeKeys(all, 2, 3));

	size_t minSize = all.size();
	size_t maxSize = 0;
	for (size_t index = 1; index <= 3; index++)
	{
		const size_t size = shard::stripeKeys(all, index, 3).size();
		minSize = std::min(minSize, size);
		maxSize = std::max(maxSize, size);
	}
	REQUIRE(maxSize - minSize <= 1);
}
