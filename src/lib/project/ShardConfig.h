#ifndef SHARD_CONFIG_H
#define SHARD_CONFIG_H

#include <set>
#include <string>

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

//! Package/crate-granular analog of stripeFilter (SW7): for language indexers
//! whose unit of work is a whole package (Rust crates, Swift SPM packages)
//! rather than a file, the file-level stripe leaves every shard indexing every
//! package. Keep only the (index-1)-th stripe of the sorted key set — each key
//! is a command's working directory — so producers split packages, not files.
//! Deterministic across machines: every shard sorts the same key set.
inline std::set<std::string> stripeKeys(
	const std::set<std::string>& keys, size_t index, size_t count)
{
	if (count <= 1)
	{
		return keys;
	}

	std::set<std::string> stripe;
	size_t position = 0;
	for (const std::string& key: keys)
	{
		if (position % count == index - 1)
		{
			stripe.insert(stripe.end(), key);
		}
		position++;
	}
	return stripe;
}
}	 // namespace shard

#endif	  // SHARD_CONFIG_H
