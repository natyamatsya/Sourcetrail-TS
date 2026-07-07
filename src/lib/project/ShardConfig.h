#ifndef SHARD_CONFIG_H
#define SHARD_CONFIG_H

#include <set>

#include "FilePath.h"

//! Distributed indexing: with a ShardConfig active, this process indexes only a
//! deterministic stripe of the project's translation units into a standalone
//! shard DB at outputPath (the live project DB is never touched). Shard DBs
//! produced for the same project with the same count are later combined with the
//! `merge` CLI command.
struct ShardConfig
{
	bool isActive() const
	{
		return count > 1;
	}

	size_t index = 1;	 //!< 1-based, <= count
	size_t count = 1;
	FilePath outputPath;
};

namespace shard
{
//! Keeps the (index-1)-th stripe of the sorted set: element positions p with
//! p % count == index-1. Deterministic across machines because std::set iterates
//! in sorted order and every shard runs on the same full set.
inline void stripeFilter(std::set<FilePath>* files, size_t index, size_t count)
{
	if (count <= 1 || files == nullptr)
	{
		return;
	}

	std::set<FilePath> stripe;
	size_t position = 0;
	for (const FilePath& path: *files)
	{
		if (position % count == index - 1)
		{
			stripe.insert(stripe.end(), path);
		}
		position++;
	}
	*files = std::move(stripe);
}
}	 // namespace shard

#endif	  // SHARD_CONFIG_H
