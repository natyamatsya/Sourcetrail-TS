#ifndef STORAGE_NODE_H
#define STORAGE_NODE_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>

#include "Id.h"
#include "LanguageMask.h"
#include "NodeKind.h"
#include "NodeModifier.h"
#endif

SRCTRL_EXPORT struct StorageNodeData
{
	StorageNodeData(): type(NODE_UNDEFINED), modifiers(NODE_MODIFIER_NONE), languages(LANGUAGE_NONE) {}

	StorageNodeData(
		NodeKind type,
		std::string serializedName,
		NodeModifierMask modifiers = NODE_MODIFIER_NONE,
		LanguageMask languages = LANGUAGE_NONE)
		: type(type)
		, serializedName(std::move(serializedName))
		, modifiers(modifiers)
		, languages(languages)
	{
	}

	// Identity is the serialized name and nothing else — the same rule the three
	// merge layers use. `languages` deliberately does NOT participate: two
	// producers recording one name must still meet, so that the merge can OR
	// their bits together and the row can report that they both claimed it.
	bool operator<(const StorageNodeData& other) const
	{
		return serializedName < other.serializedName;
	}

	NodeKind type;
	std::string serializedName;
	NodeModifierMask modifiers;
	LanguageMask languages;
};

SRCTRL_EXPORT struct StorageNode: public StorageNodeData
{
	StorageNode():  id(0) {}

	StorageNode(
		Id id,
		NodeKind type,
		std::string serializedName,
		NodeModifierMask modifiers = NODE_MODIFIER_NONE,
		LanguageMask languages = LANGUAGE_NONE)
		: StorageNodeData(type, std::move(serializedName), modifiers, languages), id(id)
	{
	}

	StorageNode(Id id, const StorageNodeData& data): StorageNodeData(data), id(id) {}

	Id id;
};

#endif	  // STORAGE_NODE_H
