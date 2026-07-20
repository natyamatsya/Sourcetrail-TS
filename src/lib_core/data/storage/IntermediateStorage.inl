// Inline implementations for IntermediateStorage.h. Included at the end of that header; not a
// standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include "LocationType.h"
#endif

inline IntermediateStorage::IntermediateStorage(): m_nextId(1) {}

inline void IntermediateStorage::clear()
{
	m_nodesIndex.clear();
	m_nodeIdIndex.clear();
	m_nodes.clear();

	m_filesIndex.clear();
	m_filesIdIndex.clear();
	m_files.clear();

	m_symbols.clear();

	m_edgesIndex.clear();
	m_edges.clear();

	m_localSymbols.clear();
	m_sourceLocations.clear();
	m_occurrences.clear();
	m_componentAccesses.clear();
	m_nodeAttributes.clear();

	m_errorsIndex.clear();
	m_errors.clear();

	m_nextId = 1;
}

inline size_t IntermediateStorage::getByteSize(size_t stringSize) const
{
	size_t byteSize = 0;

	for (const StorageFile& storageFile: getStorageFiles())
	{
		byteSize += sizeof(StorageFile);
		byteSize += stringSize + storageFile.filePath.size();
		byteSize += stringSize + storageFile.modificationTime.size();
	}

	for (const StorageErrorData& storageError: getErrors())
	{
		byteSize += sizeof(StorageErrorData);
		byteSize += stringSize + storageError.message.size();
		byteSize += stringSize + storageError.translationUnit.size();
	}

	for (const StorageNode& storageNode: getStorageNodes())
	{
		byteSize += sizeof(StorageNode);
		byteSize += stringSize + storageNode.serializedName.size();
	}

	for (const StorageLocalSymbol& storageLocalSymbol: getStorageLocalSymbols())
	{
		byteSize += sizeof(StorageLocalSymbol);
		byteSize += stringSize + storageLocalSymbol.name.size();
	}

	byteSize += sizeof(StorageEdge) * getStorageEdges().size();
	byteSize += sizeof(StorageComponentAccess) * getComponentAccesses().size();
	for (const StorageNodeAttribute& storageNodeAttribute: getNodeAttributes())
	{
		byteSize += sizeof(StorageNodeAttribute);
		byteSize += stringSize + storageNodeAttribute.value.size();
	}
	byteSize += sizeof(StorageOccurrence) * getStorageOccurrences().size();
	byteSize += sizeof(StorageSymbol) * getStorageSymbols().size();
	byteSize += sizeof(StorageSourceLocation) * getStorageSourceLocations().size();

	return byteSize;
}

inline size_t IntermediateStorage::getSourceLocationCount() const
{
	return m_sourceLocations.size();
}

inline bool IntermediateStorage::hasFatalErrors() const
{
	for (const StorageErrorData& error: m_errors)
	{
		if (error.fatal)
		{
			return true;
		}
	}

	return false;
}

inline void IntermediateStorage::setAllFilesIncomplete()
{
	for (StorageFile& file: m_files)
	{
		file.complete = false;
	}
}

inline void IntermediateStorage::setFilesWithErrorsIncomplete()
{
	std::set<Id> errorFileIds;
	for (const StorageSourceLocation& location: m_sourceLocations)
	{
		if (location.type == LocationType::ERROR)
		{
			errorFileIds.insert(location.fileNodeId);
		}
	}

	for (StorageFile& file: m_files)
	{
		if (errorFileIds.find(file.id) != errorFileIds.end())
		{
			file.complete = false;
		}
	}
}

inline std::pair<Id, bool> IntermediateStorage::addNode(const StorageNodeData& nodeData)
{
	auto it = m_nodesIndex.find(nodeData);
	if (it != m_nodesIndex.end())
	{
		StorageNode& storedNode = m_nodes[it->second];
		if (storedNode.type < nodeData.type)
		{
			storedNode.type = nodeData.type;
		}
		return std::make_pair(storedNode.id, false);
	}

	Id nodeId = m_nextId++;
	m_nodes.emplace_back(nodeId, nodeData);
	m_nodesIndex.emplace(nodeData, m_nodes.size() - 1);
	m_nodeIdIndex.emplace(nodeId, m_nodes.size() - 1);
	return std::make_pair(nodeId, true);
}

inline std::vector<Id> IntermediateStorage::addNodes(const std::vector<StorageNode>& nodes)
{
	std::vector<Id> nodeIds;
	nodeIds.reserve(nodes.size());
	for (const StorageNode& node: nodes)
	{
		nodeIds.emplace_back(addNode(node).first);
	}
	return nodeIds;
}

inline void IntermediateStorage::setNodeType(Id nodeId, NodeKind nodeType)
{
	auto it = m_nodeIdIndex.find(nodeId);
	if (it != m_nodeIdIndex.end() && m_nodes[it->second].type < nodeType)
	{
		m_nodes[it->second].type = nodeType;
	}
}

inline void IntermediateStorage::addNodeModifier(Id nodeId, NodeModifierMask modifier)
{
	auto it = m_nodeIdIndex.find(nodeId);
	if (it != m_nodeIdIndex.end())
	{
		m_nodes[it->second].modifiers |= modifier;
	}
}

inline void IntermediateStorage::addSymbol(const StorageSymbol& symbol)
{
	m_symbols.push_back(symbol);
}

inline void IntermediateStorage::addSymbols(const std::vector<StorageSymbol>& symbols)
{
	m_symbols.insert(m_symbols.end(), symbols.begin(), symbols.end());
}

inline void IntermediateStorage::addFile(const StorageFile& file)
{
	auto it = m_filesIndex.find(file);
	if (it != m_filesIndex.end())
	{
		StorageFile& storedFile = m_files[it->second];

		if (file.indexed)
		{
			storedFile.indexed = true;
		}

		if (file.complete)
		{
			storedFile.complete = true;
		}

		if (!file.languageIdentifier.empty())
		{
			storedFile.languageIdentifier = file.languageIdentifier;
		}
	}
	else
	{
		m_filesIndex.emplace(file, m_files.size());
		m_filesIdIndex.emplace(file.id, m_files.size());
		m_files.emplace_back(file);
	}
}

inline void IntermediateStorage::setFileLanguage(Id fileId, const std::string& languageIdentifier)
{
	auto it = m_filesIdIndex.find(fileId);
	if (it != m_filesIdIndex.end())
	{
		m_files[it->second].languageIdentifier = languageIdentifier;
	}
}

inline Id IntermediateStorage::addEdge(const StorageEdgeData& edgeData)
{
	auto it = m_edgesIndex.find(edgeData);
	if (it != m_edgesIndex.end())
	{
		return m_edges[it->second].id;
	}

	Id edgeId = m_nextId++;
	m_edges.emplace_back(edgeId, edgeData);
	m_edgesIndex.emplace(edgeData, m_edges.size() - 1);
	return edgeId;
}

inline std::vector<Id> IntermediateStorage::addEdges(const std::vector<StorageEdge>& edges)
{
	std::vector<Id> edgeIds;
	edgeIds.reserve(edges.size());
	for (const StorageEdge& edge: edges)
	{
		edgeIds.emplace_back(addEdge(edge));
	}
	return edgeIds;
}

inline Id IntermediateStorage::addLocalSymbol(const StorageLocalSymbolData& localSymbolData)
{
	auto it = m_localSymbols.find(StorageLocalSymbol(0, localSymbolData));
	if (it != m_localSymbols.end())
	{
		return it->id;
	}

	Id localSymbolId = m_nextId++;
	m_localSymbols.emplace(localSymbolId, localSymbolData);
	return localSymbolId;
}

inline std::vector<Id> IntermediateStorage::addLocalSymbols(const std::set<StorageLocalSymbol>& symbols)
{
	std::vector<Id> symbolIds;
	symbolIds.reserve(symbols.size());
	for (const StorageLocalSymbol& symbol: symbols)
	{
		symbolIds.emplace_back(addLocalSymbol(symbol));
	}
	return symbolIds;
}

inline Id IntermediateStorage::addSourceLocation(const StorageSourceLocationData& sourceLocationData)
{
	auto it = m_sourceLocations.find(StorageSourceLocation(0, sourceLocationData));
	if (it != m_sourceLocations.end())
	{
		return it->id;
	}

	Id sourceLocationId = m_nextId++;
	m_sourceLocations.emplace(sourceLocationId, sourceLocationData);
	return sourceLocationId;
}

inline std::vector<Id> IntermediateStorage::addSourceLocations(const std::vector<StorageSourceLocation>& locations)
{
	std::vector<Id> locationIds;
	locationIds.reserve(locations.size());
	for (const StorageSourceLocation& location: locations)
	{
		locationIds.emplace_back(addSourceLocation(location));
	}
	return locationIds;
}

inline void IntermediateStorage::addOccurrence(const StorageOccurrence& occurrence)
{
	m_occurrences.emplace(occurrence);
}

inline void IntermediateStorage::addOccurrences(const std::vector<StorageOccurrence>& occurrences)
{
	m_occurrences.insert(occurrences.begin(), occurrences.end());
}

inline void IntermediateStorage::addComponentAccess(const StorageComponentAccess& componentAccess)
{
	m_componentAccesses.emplace(componentAccess);
}

inline void IntermediateStorage::addComponentAccesses(const std::vector<StorageComponentAccess>& componentAccesses)
{
	m_componentAccesses.insert(componentAccesses.begin(), componentAccesses.end());
}

inline void IntermediateStorage::addNodeAttribute(const StorageNodeAttribute& nodeAttribute)
{
	m_nodeAttributes.emplace(nodeAttribute);
}

inline void IntermediateStorage::addNodeAttributes(const std::vector<StorageNodeAttribute>& nodeAttributes)
{
	m_nodeAttributes.insert(nodeAttributes.begin(), nodeAttributes.end());
}

inline void IntermediateStorage::addElementComponent(const StorageElementComponent& component)
{
	m_elementComponents.emplace(component);
}

inline void IntermediateStorage::addElementComponents(const std::vector<StorageElementComponent>& components)
{
	m_elementComponents.insert(components.begin(), components.end());
}

inline Id IntermediateStorage::addError(const StorageErrorData& errorData)
{
	auto it = m_errorsIndex.find(errorData);
	if (it != m_errorsIndex.end())
	{
		return m_errors[it->second].id;
	}

	Id errorId = m_nextId++;
	m_errors.emplace_back(errorId, errorData);
	m_errorsIndex.emplace(errorData, m_errors.size() - 1);
	return errorId;
}

inline const std::vector<StorageNode>& IntermediateStorage::getStorageNodes() const
{
	return m_nodes;
}

inline const std::vector<StorageFile>& IntermediateStorage::getStorageFiles() const
{
	return m_files;
}

inline const std::vector<StorageSymbol>& IntermediateStorage::getStorageSymbols() const
{
	return m_symbols;
}

inline const std::vector<StorageEdge>& IntermediateStorage::getStorageEdges() const
{
	return m_edges;
}

inline const std::set<StorageLocalSymbol>& IntermediateStorage::getStorageLocalSymbols() const
{
	return m_localSymbols;
}

inline const std::set<StorageSourceLocation>& IntermediateStorage::getStorageSourceLocations() const
{
	return m_sourceLocations;
}

inline const std::set<StorageOccurrence>& IntermediateStorage::getStorageOccurrences() const
{
	return m_occurrences;
}

inline const std::set<StorageComponentAccess>& IntermediateStorage::getComponentAccesses() const
{
	return m_componentAccesses;
}

inline const std::set<StorageNodeAttribute>& IntermediateStorage::getNodeAttributes() const
{
	return m_nodeAttributes;
}

inline const std::set<StorageElementComponent>& IntermediateStorage::getElementComponents() const
{
	return m_elementComponents;
}

inline const std::vector<StorageError>& IntermediateStorage::getErrors() const
{
	return m_errors;
}

inline void IntermediateStorage::setStorageNodes(std::vector<StorageNode> storageNodes)
{
	m_nodes = std::move(storageNodes);

	m_nodesIndex.clear();
	m_nodeIdIndex.clear();
	for (size_t i = 0; i < m_nodes.size(); i++)
	{
		m_nodesIndex.emplace(m_nodes[i], i);
		m_nodeIdIndex.emplace(m_nodes[i].id, i);
	}
}

inline void IntermediateStorage::setStorageFiles(std::vector<StorageFile> storageFiles)
{
	m_files = std::move(storageFiles);

	m_filesIndex.clear();
	m_filesIdIndex.clear();
	for (size_t i = 0; i < m_files.size(); i++)
	{
		m_filesIndex.emplace(m_files[i], i);
		m_filesIdIndex.emplace(m_files[i].id, i);
	}
}

inline void IntermediateStorage::setStorageSymbols(std::vector<StorageSymbol> storageSymbols)
{
	m_symbols = std::move(storageSymbols);
}

inline void IntermediateStorage::setStorageEdges(std::vector<StorageEdge> storageEdges)
{
	m_edges = std::move(storageEdges);

	m_edgesIndex.clear();
	for (size_t i = 0; i < m_edges.size(); i++)
	{
		m_edgesIndex.emplace(m_edges[i], i);
	}
}

inline void IntermediateStorage::setStorageLocalSymbols(std::set<StorageLocalSymbol> storageLocalSymbols)
{
	m_localSymbols = std::move(storageLocalSymbols);
}

inline void IntermediateStorage::setStorageSourceLocations(std::set<StorageSourceLocation> storageSourceLocations)
{
	m_sourceLocations = std::move(storageSourceLocations);
}

inline void IntermediateStorage::setStorageOccurrences(std::set<StorageOccurrence> storageOccurrences)
{
	m_occurrences = std::move(storageOccurrences);
}

inline void IntermediateStorage::setComponentAccesses(std::set<StorageComponentAccess> componentAccesses)
{
	m_componentAccesses = std::move(componentAccesses);
}

inline void IntermediateStorage::setNodeAttributes(std::set<StorageNodeAttribute> nodeAttributes)
{
	m_nodeAttributes = std::move(nodeAttributes);
}

inline void IntermediateStorage::setElementComponents(std::set<StorageElementComponent> components)
{
	m_elementComponents = std::move(components);
}

inline void IntermediateStorage::setErrors(std::vector<StorageError> errors)
{
	m_errors = std::move(errors);

	m_errorsIndex.clear();
	for (size_t i = 0; i < m_errors.size(); i++)
	{
		m_errorsIndex.emplace(m_errors[i], i);
	}
}

inline Id IntermediateStorage::getNextId() const
{
	return m_nextId;
}

inline void IntermediateStorage::setNextId(const Id nextId)
{
	m_nextId = nextId;
}
