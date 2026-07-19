#ifndef STORAGE_NODE_ATTRIBUTE_H
#define STORAGE_NODE_ATTRIBUTE_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>

#include "Id.h"
#include "NodeAttributeKind.h"
#endif

// A sparse, display-only fact about a node — the key→value side-table shape from
// context/DESIGN_STORAGE_CODEGEN.md. Unlike StorageComponentAccess (one row per
// node), a node may carry several attributes, so ordering is the full
// (nodeId, key, value) triple.
SRCTRL_EXPORT struct StorageNodeAttribute
{
	StorageNodeAttribute(): nodeId(0), key(NodeAttributeKind::NONE) {}

	StorageNodeAttribute(Id nodeId, NodeAttributeKind key, std::string value)
		: nodeId(nodeId), key(key), value(std::move(value))
	{
	}

	bool operator<(const StorageNodeAttribute& other) const
	{
		if (nodeId != other.nodeId)
		{
			return nodeId < other.nodeId;
		}
		if (key != other.key)
		{
			return key < other.key;
		}
		return value < other.value;
	}

	Id nodeId;
	NodeAttributeKind key;
	std::string value;
};

#endif	  // STORAGE_NODE_ATTRIBUTE_H
