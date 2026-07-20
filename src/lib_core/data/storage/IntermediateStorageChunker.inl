// Inline implementations for IntermediateStorageChunker.h. Included at the end of that header
// (classic) or via the srctrl.interprocess wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "IntermediateStorage.h"
#endif

// ODR-safe home for the cost model and the Chunker (anonymous namespaces are an ODR trap in
// headers/inls).
namespace intermediate_storage_chunker_detail
{

// Budget for one chunk's (over-)estimated serialized size. Back-pressure in
// the subprocess keeps at most two entries queued, and two chunks plus queue
// headers must stay comfortably below the fixed 16 MiB segment. Keep in sync
// with CHUNK_BUDGET_BYTES in src/rust_indexer/indexer/src/ipc/storage.rs.
inline constexpr size_t CHUNK_BUDGET_BYTES = 7 * 1024 * 1024;

// Conservative per-row estimates of the serialized footprint (table fields +
// per-row overhead + padding). Same model as the Rust chunker.
inline constexpr size_t EDGE_COST = 56;
inline constexpr size_t SYMBOL_COST = 32;
inline constexpr size_t LOCATION_COST = 64;
inline constexpr size_t OCCURRENCE_COST = 32;
inline constexpr size_t COMPONENT_ACCESS_COST = 32;

inline size_t nodeCost(const StorageNode& n)
{
	return 48 + n.serializedName.size();
}

inline size_t fileCost(const StorageFile& f)
{
	return 64 + f.filePath.size() + f.languageIdentifier.size();
}

inline size_t localSymbolCost(const StorageLocalSymbol& ls)
{
	return 48 + ls.name.size();
}

inline size_t elementComponentCost(const StorageElementComponent& ec)
{
	return 48 + ec.data.size();
}

inline size_t errorCost(const StorageError& e)
{
	return 64 + e.message.size() + e.translationUnit.size();
}

inline size_t nodeAttributeCost(const StorageNodeAttribute& a)
{
	return 40 + a.value.size();
}

inline size_t estimatedSize(const IntermediateStorage& s)
{
	size_t total = 1024;
	for (const StorageNode& n: s.getStorageNodes())
		total += nodeCost(n);
	for (const StorageFile& f: s.getStorageFiles())
		total += fileCost(f);
	total += s.getStorageEdges().size() * EDGE_COST;
	total += s.getStorageSymbols().size() * SYMBOL_COST;
	total += s.getStorageSourceLocations().size() * LOCATION_COST;
	for (const StorageLocalSymbol& ls: s.getStorageLocalSymbols())
		total += localSymbolCost(ls);
	total += s.getStorageOccurrences().size() * OCCURRENCE_COST;
	total += s.getComponentAccesses().size() * COMPONENT_ACCESS_COST;
	for (const StorageNodeAttribute& a: s.getNodeAttributes())
		total += nodeAttributeCost(a);
	for (const StorageElementComponent& ec: s.getElementComponents())
		total += elementComponentCost(ec);
	for (const StorageError& e: s.getErrors())
		total += errorCost(e);
	return total;
}

class Chunker
{
public:
	explicit Chunker(const IntermediateStorage& src): m_src(src)
	{
		for (const StorageNode& n: src.getStorageNodes())
			m_nodesById.emplace(n.id, &n);
		for (const StorageFile& f: src.getStorageFiles())
			m_filesByNodeId.emplace(f.id, &f);
		for (const StorageSymbol& s: src.getStorageSymbols())
			m_symbolsByNodeId.emplace(s.id, &s);
		for (const StorageSourceLocation& l: src.getStorageSourceLocations())
			m_locationsById.emplace(l.id, &l);
		for (const StorageOccurrence& o: src.getStorageOccurrences())
			m_occurrencesByElement[o.elementId].push_back(&o);
		for (const StorageElementComponent& ec: src.getElementComponents())
			m_elementComponentsByElement[ec.elementId].push_back(&ec);
	}

	std::vector<std::shared_ptr<IntermediateStorage>> run()
	{
		// Element groups in stable source order: every group is emitted
		// atomically into one chunk, together with the rows it references.
		for (const StorageNode& node: m_src.getStorageNodes())
		{
			const std::vector<const StorageOccurrence*> occs = takeOccurrences(node.id);
			const std::vector<const StorageElementComponent*> comps = takeElementComponents(node.id);
			ensureBudget(nodeGroupCost(node.id, occs) + componentsCost(comps));
			addNode(node.id, /*withSymbol=*/true);
			addOccurrences(occs);
			addElementComponents(comps);
		}
		for (const StorageEdge& edge: m_src.getStorageEdges())
		{
			const std::vector<const StorageOccurrence*> occs = takeOccurrences(edge.id);
			const std::vector<const StorageElementComponent*> comps = takeElementComponents(edge.id);
			ensureBudget(
				EDGE_COST + nodeGroupCost(edge.sourceNodeId, {}) +
				nodeGroupCost(edge.targetNodeId, {}) + occurrencesCost(occs) +
				componentsCost(comps));
			addNode(edge.sourceNodeId, false);
			addNode(edge.targetNodeId, false);
			m_curEdges.push_back(edge);
			m_curCost += EDGE_COST;
			addOccurrences(occs);
			addElementComponents(comps);
		}
		for (const StorageLocalSymbol& ls: m_src.getStorageLocalSymbols())
		{
			const std::vector<const StorageOccurrence*> occs = takeOccurrences(ls.id);
			ensureBudget(localSymbolCost(ls) + occurrencesCost(occs));
			m_curLocalSymbols.insert(ls);
			m_curCost += localSymbolCost(ls);
			addOccurrences(occs);
		}
		// Dangling occurrences and element components (element not in this
		// storage) — keep them so chunked inject behaves like monolithic
		// inject.
		for (const auto& [elementId, occs]: m_occurrencesByElement)
		{
			ensureBudget(occurrencesCost(occs));
			addOccurrences(occs);
		}
		for (const auto& [elementId, comps]: m_elementComponentsByElement)
		{
			ensureBudget(componentsCost(comps));
			addElementComponents(comps);
		}
		// Locations with no occurrence at all.
		for (const StorageSourceLocation& loc: m_src.getStorageSourceLocations())
		{
			if (m_emittedLocations.count(loc.id))
				continue;
			ensureBudget(LOCATION_COST + STUB_COST_MAX);
			addLocation(loc.id);
		}
		for (const StorageComponentAccess& ca: m_src.getComponentAccesses())
		{
			ensureBudget(COMPONENT_ACCESS_COST + STUB_COST_MAX);
			addNode(ca.nodeId, false);
			m_curComponentAccesses.insert(ca);
			m_curCost += COMPONENT_ACCESS_COST;
		}
		for (const StorageNodeAttribute& a: m_src.getNodeAttributes())
		{
			ensureBudget(nodeAttributeCost(a) + STUB_COST_MAX);
			addNode(a.nodeId, false);
			m_curNodeAttributes.insert(a);
			m_curCost += nodeAttributeCost(a);
		}
		for (const StorageError& err: m_src.getErrors())
		{
			ensureBudget(errorCost(err));
			m_curErrors.push_back(err);
			m_curCost += errorCost(err);
		}
		flush();
		return std::move(m_chunks);
	}

private:
	// Upper bound for one node stub (node row + potential file row).
	static constexpr size_t STUB_COST_MAX = 512;

	std::vector<const StorageOccurrence*> takeOccurrences(Id elementId)
	{
		const auto it = m_occurrencesByElement.find(elementId);
		if (it == m_occurrencesByElement.end())
			return {};
		std::vector<const StorageOccurrence*> occs = std::move(it->second);
		m_occurrencesByElement.erase(it);
		return occs;
	}

	std::vector<const StorageElementComponent*> takeElementComponents(Id elementId)
	{
		const auto it = m_elementComponentsByElement.find(elementId);
		if (it == m_elementComponentsByElement.end())
			return {};
		std::vector<const StorageElementComponent*> comps = std::move(it->second);
		m_elementComponentsByElement.erase(it);
		return comps;
	}

	size_t nodeGroupCost(Id nodeId, const std::vector<const StorageOccurrence*>& occs) const
	{
		size_t cost = SYMBOL_COST;
		if (const auto it = m_nodesById.find(nodeId); it != m_nodesById.end())
			cost += nodeCost(*it->second);
		if (const auto it = m_filesByNodeId.find(nodeId); it != m_filesByNodeId.end())
			cost += fileCost(*it->second);
		return cost + occurrencesCost(occs);
	}

	// Pessimistic cost of occurrence rows plus their location rows and the
	// locations' file stubs.
	size_t occurrencesCost(const std::vector<const StorageOccurrence*>& occs) const
	{
		return occs.size() * (OCCURRENCE_COST + LOCATION_COST) +
			std::min<size_t>(occs.size(), 4) * STUB_COST_MAX;
	}

	size_t componentsCost(const std::vector<const StorageElementComponent*>& comps) const
	{
		size_t cost = 0;
		for (const StorageElementComponent* c: comps)
			cost += elementComponentCost(*c);
		return cost;
	}

	// Start a new chunk if adding `groupCost` would exceed the budget. A
	// single over-budget group still goes into one (oversized) chunk — in
	// practice groups are tiny compared to the budget.
	void ensureBudget(size_t groupCost)
	{
		if (m_curCost > 0 && m_curCost + groupCost > CHUNK_BUDGET_BYTES)
			flush();
	}

	void flush()
	{
		if (m_curCost == 0)
			return;
		auto chunk = std::make_shared<IntermediateStorage>();
		chunk->setStorageNodes(std::move(m_curNodes));
		chunk->setStorageFiles(std::move(m_curFiles));
		chunk->setStorageSymbols(std::move(m_curSymbols));
		chunk->setStorageEdges(std::move(m_curEdges));
		chunk->setStorageLocalSymbols(std::move(m_curLocalSymbols));
		chunk->setStorageSourceLocations(std::move(m_curLocations));
		chunk->setStorageOccurrences(std::move(m_curOccurrences));
		chunk->setComponentAccesses(std::move(m_curComponentAccesses));
		chunk->setNodeAttributes(std::move(m_curNodeAttributes));
		chunk->setElementComponents(std::move(m_curElementComponents));
		chunk->setErrors(std::move(m_curErrors));
		chunk->setNextId(m_src.getNextId());
		m_chunks.push_back(std::move(chunk));

		m_curNodes = {};
		m_curFiles = {};
		m_curSymbols = {};
		m_curEdges = {};
		m_curLocalSymbols = {};
		m_curLocations = {};
		m_curOccurrences = {};
		m_curComponentAccesses = {};
		m_curNodeAttributes = {};
		m_curElementComponents = {};
		m_curErrors = {};
		m_curCost = 0;
		m_curNodeIds.clear();
		m_curLocationIds.clear();
	}

	// Add a node row (plus its file row for file nodes) to the current chunk
	// if not present. `withSymbol` adds the node's symbol row — only the
	// node's home group does this (symbols are not deduped on inject).
	void addNode(Id nodeId, bool withSymbol)
	{
		if (m_curNodeIds.insert(nodeId).second)
		{
			if (const auto it = m_nodesById.find(nodeId); it != m_nodesById.end())
			{
				m_curNodes.push_back(*it->second);
				m_curCost += nodeCost(*it->second);
			}
			if (const auto it = m_filesByNodeId.find(nodeId); it != m_filesByNodeId.end())
			{
				m_curFiles.push_back(*it->second);
				m_curCost += fileCost(*it->second);
			}
		}
		if (withSymbol)
		{
			if (const auto it = m_symbolsByNodeId.find(nodeId); it != m_symbolsByNodeId.end())
			{
				m_curSymbols.push_back(*it->second);
				m_curCost += SYMBOL_COST;
			}
		}
	}

	// Add a location row (plus its file node stub) to the current chunk if
	// not present. Locations dedup by position on inject, so a location
	// repeated in a later chunk merges onto the same id.
	void addLocation(Id locationId)
	{
		const auto it = m_locationsById.find(locationId);
		if (it == m_locationsById.end())
			return;
		if (!m_curLocationIds.insert(locationId).second)
			return;
		addNode(it->second->fileNodeId, false);
		m_curLocations.insert(*it->second);
		m_curCost += LOCATION_COST;
		m_emittedLocations.insert(locationId);
	}

	void addOccurrences(const std::vector<const StorageOccurrence*>& occs)
	{
		for (const StorageOccurrence* o: occs)
		{
			addLocation(o->sourceLocationId);
			m_curOccurrences.insert(*o);
			m_curCost += OCCURRENCE_COST;
		}
	}

	void addElementComponents(const std::vector<const StorageElementComponent*>& comps)
	{
		for (const StorageElementComponent* c: comps)
		{
			m_curElementComponents.insert(*c);
			m_curCost += elementComponentCost(*c);
		}
	}

	const IntermediateStorage& m_src;
	std::unordered_map<Id, const StorageNode*> m_nodesById;
	std::unordered_map<Id, const StorageFile*> m_filesByNodeId;
	std::unordered_map<Id, const StorageSymbol*> m_symbolsByNodeId;
	std::unordered_map<Id, const StorageSourceLocation*> m_locationsById;
	// element id → occurrence rows, in set order
	std::map<Id, std::vector<const StorageOccurrence*>> m_occurrencesByElement;
	std::map<Id, std::vector<const StorageElementComponent*>> m_elementComponentsByElement;
	// locations already emitted into some chunk (for the orphan pass)
	std::unordered_set<Id> m_emittedLocations;

	std::vector<std::shared_ptr<IntermediateStorage>> m_chunks;
	std::vector<StorageNode> m_curNodes;
	std::vector<StorageFile> m_curFiles;
	std::vector<StorageSymbol> m_curSymbols;
	std::vector<StorageEdge> m_curEdges;
	std::set<StorageLocalSymbol> m_curLocalSymbols;
	std::set<StorageSourceLocation> m_curLocations;
	std::set<StorageOccurrence> m_curOccurrences;
	std::set<StorageComponentAccess> m_curComponentAccesses;
	std::set<StorageNodeAttribute> m_curNodeAttributes;
	std::set<StorageElementComponent> m_curElementComponents;
	std::vector<StorageError> m_curErrors;
	size_t m_curCost = 0;
	std::unordered_set<Id> m_curNodeIds;
	std::unordered_set<Id> m_curLocationIds;
};

}	 // namespace intermediate_storage_chunker_detail

namespace utility
{

inline std::vector<std::shared_ptr<IntermediateStorage>> chunkIntermediateStorage(
	const std::shared_ptr<IntermediateStorage>& storage)
{
	if (intermediate_storage_chunker_detail::estimatedSize(*storage) <=
		intermediate_storage_chunker_detail::CHUNK_BUDGET_BYTES)
		return {storage};
	return intermediate_storage_chunker_detail::Chunker(*storage).run();
}

}	 // namespace utility
