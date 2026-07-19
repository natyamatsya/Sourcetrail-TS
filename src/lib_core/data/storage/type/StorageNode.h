#ifndef STORAGE_NODE_H
#define STORAGE_NODE_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>

#include "Id.h"
#include "NodeKind.h"
#include "NodeModifier.h"
#endif

SRCTRL_EXPORT struct StorageNodeData
{
	StorageNodeData(): type(NODE_UNDEFINED), modifiers(NODE_MODIFIER_NONE) {}

	StorageNodeData(NodeKind type, std::string serializedName, NodeModifierMask modifiers = NODE_MODIFIER_NONE)
		: type(type), serializedName(std::move(serializedName)), modifiers(modifiers)
	{
	}

	bool operator<(const StorageNodeData& other) const
	{
		return serializedName < other.serializedName;
	}

	NodeKind type;
	std::string serializedName;
	NodeModifierMask modifiers;
};

SRCTRL_EXPORT struct StorageNode: public StorageNodeData
{
	StorageNode():  id(0) {}

	StorageNode(Id id, NodeKind type, std::string serializedName, NodeModifierMask modifiers = NODE_MODIFIER_NONE)
		: StorageNodeData(type, std::move(serializedName), modifiers), id(id)
	{
	}

	StorageNode(Id id, const StorageNodeData& data): StorageNodeData(data), id(id) {}

	Id id;
};

#endif	  // STORAGE_NODE_H
