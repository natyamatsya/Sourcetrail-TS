// Inline implementations for Storage.h (included at the end of that header). All members are inline
// because an out-of-line member of an exported module class does not resolve for importers.

#pragma once

inline Storage::Storage() = default;

inline void Storage::inject(Storage* injected)
{
	std::lock_guard<std::mutex> lock(m_dataMutex);

	std::map<Id, Id> injectedIdToOwnElementId;
	std::map<Id, Id> injectedIdToOwnSourceLocationId;

	TRACE();
	startInjection();

	{
		// TRACE("inject errors");

		for (const StorageError& error: injected->getErrors())
		{
			Id errorId = addError(error);
			injectedIdToOwnElementId.emplace(error.id, errorId);
		}
	}

	{
		// TRACE("inject nodes");

		const std::vector<StorageNode>& nodes = injected->getStorageNodes();

		std::vector<Id> nodeIds = addNodes(nodes);

		for (std::size_t i = 0; i < nodes.size(); i++)
		{
			if (nodeIds[i])
			{
				injectedIdToOwnElementId.emplace(nodes[i].id, nodeIds[i]);
			}
		}
	}

	{
		// TRACE("inject files");

		for (const StorageFile& file: injected->getStorageFiles())
		{
			auto it = injectedIdToOwnElementId.find(file.id);
			if (it != injectedIdToOwnElementId.end())
			{
				addFile(StorageFile(
					it->second,
					file.filePath,
					file.languageIdentifier,
					file.modificationTime,
					file.indexed,
					file.complete));
			}
		}
	}

	{
		// TRACE("inject symbols");

		std::vector<StorageSymbol> symbols = injected->getStorageSymbols();
		for (std::size_t i = 0; i < symbols.size(); i++)
		{
			auto it = injectedIdToOwnElementId.find(symbols[i].id);
			if (it != injectedIdToOwnElementId.end())
			{
				symbols[i].id = it->second;
			}
			else
			{
				srctrl::log::warning("New symbol id could not be found.");
				symbols.erase(symbols.begin() + i);
				i--;
			}
		}

		addSymbols(symbols);
	}

	{
		// TRACE("inject edges");

		std::vector<StorageEdge> edges = injected->getStorageEdges();
		for (std::size_t i = 0; i < edges.size(); i++)
		{
			StorageEdge& edge = edges[i];
			std::size_t updateCount = 0;

			auto it = injectedIdToOwnElementId.find(edge.sourceNodeId);
			if (it != injectedIdToOwnElementId.end())
			{
				edge.sourceNodeId = it->second;
				updateCount++;
			}

			it = injectedIdToOwnElementId.find(edge.targetNodeId);
			if (it != injectedIdToOwnElementId.end())
			{
				edge.targetNodeId = it->second;
				updateCount++;
			}

			if (updateCount != 2)
			{
				srctrl::log::warning("New edge source or target id could not be found.");
				edges.erase(edges.begin() + i);
				i--;
			}
		}

		std::vector<Id> edgeIds = addEdges(edges);

		if (edges.size() == edgeIds.size())
		{
			for (std::size_t i = 0; i < edgeIds.size(); i++)
			{
				if (edgeIds[i])
				{
					injectedIdToOwnElementId.emplace(edges[i].id, edgeIds[i]);
				}
			}
		}
		else
		{
			srctrl::log::error("Returned edge ids don't match injected count.");
		}
	}

	{
		// TRACE("inject local symbols");

		const std::set<StorageLocalSymbol>& symbols = injected->getStorageLocalSymbols();
		std::vector<Id> symbolIds = addLocalSymbols(symbols);

		auto it = symbols.begin();
		for (std::size_t i = 0; i < symbols.size(); i++)
		{
			if (symbolIds[i])
			{
				injectedIdToOwnElementId.emplace(it->id, symbolIds[i]);
			}
			it++;
		}
	}

	{
		// TRACE("inject locations");

		const std::set<StorageSourceLocation>& oldLocations = injected->getStorageSourceLocations();
		std::vector<StorageSourceLocation> locations;
		locations.reserve(oldLocations.size());

		for (const StorageSourceLocation& location: oldLocations)
		{
			auto it = injectedIdToOwnElementId.find(location.fileNodeId);
			if (it != injectedIdToOwnElementId.end())
			{
				const Id ownFileNodeId = it->second;
				locations.emplace_back(
					location.id,
					ownFileNodeId,
					location.startLine,
					location.startCol,
					location.endLine,
					location.endCol,
					location.type);
			}
		}

		std::vector<Id> locationIds = addSourceLocations(locations);

		if (locations.size() == locationIds.size())
		{
			for (std::size_t i = 0; i < locationIds.size(); i++)
			{
				if (locationIds[i])
				{
					injectedIdToOwnSourceLocationId.emplace(locations[i].id, locationIds[i]);
				}
			}
		}
		else
		{
			srctrl::log::error("Returned source locations ids don't match injected count.");
		}
	}

	{
		// TRACE("inject occurrences");

		const std::set<StorageOccurrence>& oldOccurrences = injected->getStorageOccurrences();

		std::vector<StorageOccurrence> occurrences;
		occurrences.reserve(oldOccurrences.size());

		for (const StorageOccurrence& occurrence: oldOccurrences)
		{
			Id elementId = 0;
			Id sourceLocationId = 0;

			auto it = injectedIdToOwnElementId.find(occurrence.elementId);
			if (it != injectedIdToOwnElementId.end())
			{
				elementId = it->second;
			}

			it = injectedIdToOwnSourceLocationId.find(occurrence.sourceLocationId);
			if (it != injectedIdToOwnSourceLocationId.end())
			{
				sourceLocationId = it->second;
			}

			if (!elementId)
			{
				srctrl::log::warning("New occurrence element id could not be found.");
			}
			else if (!sourceLocationId)
			{
				srctrl::log::warning("New occurrence location id could not be found.");
			}
			else
			{
				occurrences.emplace_back(elementId, sourceLocationId);
			}
		}

		addOccurrences(occurrences);
	}

	{
		// TRACE("inject element components");

		const std::set<StorageElementComponent>& oldComponents = injected->getElementComponents();
		std::vector<StorageElementComponent> components;
		components.reserve(oldComponents.size());

		for (const StorageElementComponent& component: oldComponents)
		{
			auto it = injectedIdToOwnElementId.find(component.elementId);
			if (it != injectedIdToOwnElementId.end())
			{
				components.emplace_back(it->second, component.type, component.data);
			}
		}

		addElementComponents(components);
	}

	{
		// TRACE("inject accesses");

		const std::set<StorageComponentAccess>& oldAccesses = injected->getComponentAccesses();
		std::vector<StorageComponentAccess> accesses;
		accesses.reserve(oldAccesses.size());

		for (const StorageComponentAccess& access: oldAccesses)
		{
			auto it = injectedIdToOwnElementId.find(access.nodeId);
			if (it != injectedIdToOwnElementId.end())
			{
				accesses.emplace_back(it->second, access.type);
			}
		}

		addComponentAccesses(accesses);
	}

	{
		// TRACE("inject node attributes");

		const std::set<StorageNodeAttribute>& oldAttributes = injected->getNodeAttributes();
		std::vector<StorageNodeAttribute> attributes;
		attributes.reserve(oldAttributes.size());

		for (const StorageNodeAttribute& attribute: oldAttributes)
		{
			auto it = injectedIdToOwnElementId.find(attribute.nodeId);
			if (it != injectedIdToOwnElementId.end())
			{
				attributes.emplace_back(it->second, attribute.key, attribute.value);
			}
		}

		addNodeAttributes(attributes);
	}

	finishInjection();
}

inline void Storage::startInjection()
{
	// may be implemented in derived
}

inline void Storage::finishInjection()
{
	// may be implemented in derived
}
