#ifndef CONCURRENT_STORAGE_INDEX_H
#define CONCURRENT_STORAGE_INDEX_H

// Thread-safe element-id allocation + cross-batch dedup for the Phase 4
// concurrent-writer path.
//
// With multiple writer threads committing to Turso via BEGIN CONCURRENT, the id
// assignment and node/edge deduplication that SqliteIndexStorage does today with
// single-thread maps (m_nextElementId, m_tempNodeNameIndex, m_tempEdgeIndex, ...)
// becomes shared state. Correctness cannot come from reading the DB — MVCC
// snapshot isolation hides other writers' uncommitted rows — so it must be
// coordinated *in-process* here: one atomic id counter shared by per-kind
// concurrent intern maps, so that concurrent intern() of the same key from many
// threads yields exactly one id, and distinct keys never collide.
//
// Deliberately Qt-free and header-only (uses the raw `long long` == Id::type)
// so it can be unit-tested in isolation. SqliteIndexStorage converts Id<->long
// long at the boundary, as it already does.

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace storage
{

using ElementId = long long;  // == Id::type

//! Monotonic, thread-safe element-id allocator. Seed once from MAX(id)+1 so the
//! sequence continues an existing database.
class AtomicIdCounter
{
public:
	void seed(ElementId first) noexcept { m_next.store(first, std::memory_order_relaxed); }

	//! One fresh id.
	ElementId next() noexcept { return m_next.fetch_add(1, std::memory_order_relaxed); }

	//! A contiguous disjoint range [base, base + n); returns base.
	ElementId allocateRange(std::size_t n) noexcept
	{
		return m_next.fetch_add(static_cast<ElementId>(n), std::memory_order_relaxed);
	}

	//! Next id that would be handed out (for assertions / seeding a successor).
	ElementId peek() const noexcept { return m_next.load(std::memory_order_relaxed); }

private:
	std::atomic<ElementId> m_next{1};
};

//! Sharded concurrent intern table: maps a key to a stable element id,
//! allocating a fresh id from the shared counter the first time the key is seen.
//! Concurrent intern() of the same key yields exactly one id (the first writer to
//! win its shard lock creates it; the rest observe it).
template <class Key, class Hash = std::hash<Key>, class Eq = std::equal_to<Key>>
class ConcurrentInternMap
{
public:
	struct Result
	{
		ElementId id;
		bool created;  //!< true exactly once per distinct key, across all threads
	};

	explicit ConcurrentInternMap(AtomicIdCounter& counter, std::size_t shardCount = 64)
		: m_counter(counter)
	{
		if (shardCount == 0)
		{
			shardCount = 1;
		}
		m_shards.reserve(shardCount);
		for (std::size_t i = 0; i < shardCount; ++i)
		{
			m_shards.push_back(std::make_unique<Shard>());
		}
	}

	Result intern(const Key& key)
	{
		Shard& s = shardFor(key);
		std::lock_guard<std::mutex> lock(s.mtx);
		auto it = s.map.find(key);
		if (it != s.map.end())
		{
			return {it->second, false};
		}
		const ElementId id = m_counter.next();
		s.map.emplace(key, id);
		return {id, true};
	}

	//! Lookup without creating. Returns false if absent.
	bool find(const Key& key, ElementId& out) const
	{
		const Shard& s = shardFor(key);
		std::lock_guard<std::mutex> lock(s.mtx);
		auto it = s.map.find(key);
		if (it == s.map.end())
		{
			return false;
		}
		out = it->second;
		return true;
	}

	std::size_t size() const
	{
		std::size_t total = 0;
		for (const auto& s : m_shards)
		{
			std::lock_guard<std::mutex> lock(s->mtx);
			total += s->map.size();
		}
		return total;
	}

private:
	struct Shard
	{
		mutable std::mutex mtx;
		std::unordered_map<Key, ElementId, Hash, Eq> map;
	};

	Shard& shardFor(const Key& key) { return *m_shards[m_hash(key) % m_shards.size()]; }
	const Shard& shardFor(const Key& key) const { return *m_shards[m_hash(key) % m_shards.size()]; }

	std::vector<std::unique_ptr<Shard>> m_shards;
	AtomicIdCounter& m_counter;
	Hash m_hash;
};

//! Sharded concurrent set. `markNew(key)` returns true exactly once per distinct
//! key, across all threads — the caller that gets true "owns" inserting that row.
//! Used for kinds that dedup but allocate no new id (their PK is an existing id
//! or a composite): symbols, files, component accesses, occurrences.
template <class Key, class Hash = std::hash<Key>, class Eq = std::equal_to<Key>>
class ConcurrentDedupSet
{
public:
	explicit ConcurrentDedupSet(std::size_t shardCount = 64)
	{
		if (shardCount == 0)
		{
			shardCount = 1;
		}
		m_shards.reserve(shardCount);
		for (std::size_t i = 0; i < shardCount; ++i)
		{
			m_shards.push_back(std::make_unique<Shard>());
		}
	}

	bool markNew(const Key& key)
	{
		Shard& s = *m_shards[m_hash(key) % m_shards.size()];
		std::lock_guard<std::mutex> lock(s.mtx);
		return s.set.insert(key).second;
	}

private:
	struct Shard
	{
		std::mutex mtx;
		std::unordered_set<Key, Hash, Eq> set;
	};
	std::vector<std::unique_ptr<Shard>> m_shards;
	Hash m_hash;
};

namespace detail
{
inline std::size_t hashMix(std::size_t h, std::size_t v) noexcept
{
	return h ^ (v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
}
}  // namespace detail

//! Like ConcurrentInternMap but also tracks the maximum node *type* seen for
//! each name. Concurrent writers insert a node with its first-seen type; a later
//! batch may see the same node with a higher type (forward decl -> definition).
//! forEachIdMaxType() lets the owner reconcile the stored types to their max in a
//! single-threaded pass after draining (an inline UPDATE would MVCC-conflict).
class ConcurrentNodeIndex
{
public:
	struct Result
	{
		ElementId id;
		bool created;
	};

	explicit ConcurrentNodeIndex(AtomicIdCounter& counter, std::size_t shardCount = 64)
		: m_counter(counter)
	{
		if (shardCount == 0)
		{
			shardCount = 1;
		}
		m_shards.reserve(shardCount);
		for (std::size_t i = 0; i < shardCount; ++i)
		{
			m_shards.push_back(std::make_unique<Shard>());
		}
	}

	Result intern(const std::string& name, int type)
	{
		Shard& s = *m_shards[m_hash(name) % m_shards.size()];
		std::lock_guard<std::mutex> lock(s.mtx);
		auto it = s.map.find(name);
		if (it != s.map.end())
		{
			if (type > it->second.maxType)
			{
				it->second.maxType = type;
			}
			return {it->second.id, false};
		}
		const ElementId id = m_counter.next();
		s.map.emplace(name, Entry{id, type});
		return {id, true};
	}

	//! Visit (id, maxType) for every node. Callback runs under the shard lock.
	template <class F>
	void forEachIdMaxType(F&& f) const
	{
		for (const auto& s: m_shards)
		{
			std::lock_guard<std::mutex> lock(s->mtx);
			for (const auto& [name, entry]: s->map)
			{
				f(entry.id, entry.maxType);
			}
		}
	}

private:
	struct Entry
	{
		ElementId id;
		int maxType;
	};
	struct Shard
	{
		mutable std::mutex mtx;
		std::unordered_map<std::string, Entry> map;
	};
	std::vector<std::unique_ptr<Shard>> m_shards;
	AtomicIdCounter& m_counter;
	std::hash<std::string> m_hash;
};

//! Edge dedup key: (type, source element id, target element id).
struct EdgeKey
{
	int type;
	ElementId source;
	ElementId target;

	bool operator==(const EdgeKey& o) const noexcept
	{
		return type == o.type && source == o.source && target == o.target;
	}
};

struct EdgeKeyHash
{
	std::size_t operator()(const EdgeKey& k) const noexcept
	{
		auto mix = [](std::size_t h, std::size_t v) noexcept
		{
			return h ^ (v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
		};
		std::size_t h = std::hash<int>{}(k.type);
		h = mix(h, std::hash<ElementId>{}(k.source));
		h = mix(h, std::hash<ElementId>{}(k.target));
		return h;
	}
};

//! Source-location dedup key (matches the SQLite source_location unique tuple).
struct SourceLocationKey
{
	ElementId fileNodeId;
	int startLine;
	int startCol;
	int endLine;
	int endCol;
	int type;

	bool operator==(const SourceLocationKey& o) const noexcept
	{
		return fileNodeId == o.fileNodeId && startLine == o.startLine && startCol == o.startCol &&
			endLine == o.endLine && endCol == o.endCol && type == o.type;
	}
};

struct SourceLocationKeyHash
{
	std::size_t operator()(const SourceLocationKey& k) const noexcept
	{
		std::size_t h = std::hash<ElementId>{}(k.fileNodeId);
		h = detail::hashMix(h, static_cast<std::size_t>(k.startLine));
		h = detail::hashMix(h, static_cast<std::size_t>(k.startCol));
		h = detail::hashMix(h, static_cast<std::size_t>(k.endLine));
		h = detail::hashMix(h, static_cast<std::size_t>(k.endCol));
		h = detail::hashMix(h, static_cast<std::size_t>(k.type));
		return h;
	}
};

//! Element-component dedup key: (element id, type, data).
struct ElementComponentKey
{
	ElementId elementId;
	int type;
	std::string data;

	bool operator==(const ElementComponentKey& o) const noexcept
	{
		return elementId == o.elementId && type == o.type && data == o.data;
	}
};

struct ElementComponentKeyHash
{
	std::size_t operator()(const ElementComponentKey& k) const noexcept
	{
		std::size_t h = std::hash<ElementId>{}(k.elementId);
		h = detail::hashMix(h, static_cast<std::size_t>(k.type));
		h = detail::hashMix(h, std::hash<std::string>{}(k.data));
		return h;
	}
};

//! Occurrence dedup key: (element id, source-location id) — the occurrence PK.
struct OccurrenceKey
{
	ElementId elementId;
	ElementId sourceLocationId;

	bool operator==(const OccurrenceKey& o) const noexcept
	{
		return elementId == o.elementId && sourceLocationId == o.sourceLocationId;
	}
};

struct OccurrenceKeyHash
{
	std::size_t operator()(const OccurrenceKey& k) const noexcept
	{
		return detail::hashMix(std::hash<ElementId>{}(k.elementId),
			std::hash<ElementId>{}(k.sourceLocationId));
	}
};

//! The coordinator SqliteIndexStorage would hold: one id counter shared by the
//! per-kind dedup maps/sets. Kinds that mint a fresh element id use InternMaps
//! (nodes, edges, local symbols, source locations, element components, errors);
//! kinds keyed by an existing/composite id only dedup, via DedupSets (symbols,
//! files, component accesses, occurrences). All ids come from one counter so the
//! element-id space is globally unique.
class ConcurrentStorageIndex
{
public:
	using Result = ConcurrentInternMap<std::string>::Result;

	ConcurrentStorageIndex()
		: m_nodes(m_counter)
		, m_localSymbolNames(m_counter)
		, m_edges(m_counter)
		, m_sourceLocations(m_counter)
		, m_elementComponents(m_counter)
		, m_errors(m_counter)
	{
	}

	void seed(ElementId firstId) noexcept { m_counter.seed(firstId); }

	AtomicIdCounter& counter() noexcept { return m_counter; }

	// Id-minting kinds.
	ConcurrentNodeIndex::Result internNode(const std::string& serializedName, int type)
	{
		return m_nodes.intern(serializedName, type);
	}
	//! Visit (nodeId, maxType) — for reconciling node types to their max after draining.
	template <class F>
	void forEachNode(F&& f) const
	{
		m_nodes.forEachIdMaxType(std::forward<F>(f));
	}
	Result internLocalSymbol(const std::string& name) { return m_localSymbolNames.intern(name); }

	ConcurrentInternMap<EdgeKey, EdgeKeyHash>::Result internEdge(int type, ElementId source, ElementId target)
	{
		return m_edges.intern(EdgeKey{type, source, target});
	}

	ConcurrentInternMap<SourceLocationKey, SourceLocationKeyHash>::Result internSourceLocation(
		const SourceLocationKey& key)
	{
		return m_sourceLocations.intern(key);
	}

	ConcurrentInternMap<ElementComponentKey, ElementComponentKeyHash>::Result internElementComponent(
		const ElementComponentKey& key)
	{
		return m_elementComponents.intern(key);
	}

	Result internError(const std::string& message, bool fatal)
	{
		return m_errors.intern(message + '\x1f' + (fatal ? '1' : '0'));
	}

	// Dedup-only kinds (return true for the one caller that should insert).
	bool markSymbol(ElementId nodeId) { return m_symbols.markNew(nodeId); }
	bool markFile(const std::string& path) { return m_files.markNew(path); }
	bool markComponentAccess(ElementId nodeId) { return m_componentAccesses.markNew(nodeId); }
	// A node may carry several attributes, so dedup on the (node, key, value) triple.
	bool markNodeAttribute(ElementId nodeId, int key, const std::string& value)
	{
		return m_nodeAttributes.markNew(
			std::to_string(nodeId) + '\x1f' + std::to_string(key) + '\x1f' + value);
	}
	bool markOccurrence(ElementId elementId, ElementId sourceLocationId)
	{
		return m_occurrences.markNew(OccurrenceKey{elementId, sourceLocationId});
	}

private:
	AtomicIdCounter m_counter;
	ConcurrentNodeIndex m_nodes;
	ConcurrentInternMap<std::string> m_localSymbolNames;
	ConcurrentInternMap<EdgeKey, EdgeKeyHash> m_edges;
	ConcurrentInternMap<SourceLocationKey, SourceLocationKeyHash> m_sourceLocations;
	ConcurrentInternMap<ElementComponentKey, ElementComponentKeyHash> m_elementComponents;
	ConcurrentInternMap<std::string> m_errors;

	ConcurrentDedupSet<ElementId> m_symbols;
	ConcurrentDedupSet<std::string> m_files;
	ConcurrentDedupSet<ElementId> m_componentAccesses;
	ConcurrentDedupSet<std::string> m_nodeAttributes;
	ConcurrentDedupSet<OccurrenceKey, OccurrenceKeyHash> m_occurrences;
};

}  // namespace storage

#endif  // CONCURRENT_STORAGE_INDEX_H
