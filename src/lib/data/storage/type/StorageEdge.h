#ifndef STORAGE_EDGE_H
#define STORAGE_EDGE_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "Edge.h"
#endif

SRCTRL_EXPORT struct StorageEdgeData
{
	StorageEdgeData(): type(Edge::EDGE_UNDEFINED), sourceNodeId(0), targetNodeId(0) {}

	StorageEdgeData(Edge::EdgeType type, Id sourceNodeId, Id targetNodeId)
		: type(type), sourceNodeId(sourceNodeId), targetNodeId(targetNodeId)
	{
	}

	bool operator<(const StorageEdgeData& other) const
	{
		if (type != other.type)
		{
			return type < other.type;
		}
		else if (sourceNodeId != other.sourceNodeId)
		{
			return sourceNodeId < other.sourceNodeId;
		}
		else
		{
			return targetNodeId < other.targetNodeId;
		}
	}

	Edge::EdgeType type;
	Id sourceNodeId;
	Id targetNodeId;
};

SRCTRL_EXPORT struct StorageEdge: public StorageEdgeData
{
	StorageEdge():  id(0) {}

	StorageEdge(Id id, const StorageEdgeData& data): StorageEdgeData(data), id(id) {}

	StorageEdge(Id id, Edge::EdgeType type, Id sourceNodeId, Id targetNodeId)
		: StorageEdgeData(type, sourceNodeId, targetNodeId), id(id)
	{
	}

	Id id;
};

#endif	  // STORAGE_EDGE_H
