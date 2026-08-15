#ifndef GROUP_TYPE_H
#define GROUP_TYPE_H

#include <string>

enum class GroupType
{
	NONE,
	DEFAULT,
	FRAMELESS,
	FILE,
	NAMESPACE,
	INHERITANCE,
	// One group per producing language, plus a group for the nodes more than
	// one language claims. Unlike FILE and NAMESPACE this groups by a property
	// of the node itself rather than by a parent, so it needs no storage
	// lookup -- the mask is already on the graph node.
	// See context/DESIGN_XLANG_BOUNDARIES.md.
	LANGUAGE
};

std::string groupTypeToString(GroupType type);
GroupType stringToGroupType(const std::string& value);

enum class GroupLayout
{
	LIST,
	SKEWED,
	BUCKET,
	SQUARE
};

#endif	  // GROUP_TYPE_H
