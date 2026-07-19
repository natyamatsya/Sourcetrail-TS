#ifndef NODE_TYPE_SET_H
#define NODE_TYPE_SET_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <functional>
#include <vector>

#include "Id.h"
#include "NodeType.h"

class NodeType;
#endif

SRCTRL_EXPORT class NodeTypeSet
{
public:
	typedef unsigned long int MaskType;

	static NodeTypeSet all();
	static NodeTypeSet none();

	NodeTypeSet();
	NodeTypeSet(const NodeType& type);

	bool operator==(const NodeTypeSet& other) const;
	bool operator!=(const NodeTypeSet& other) const;

	std::vector<NodeType> getNodeTypes() const;

	void invert();
	NodeTypeSet getInverse() const;

	void add(const NodeTypeSet& typeSet);
	NodeTypeSet getWithAdded(const NodeTypeSet& typeSet) const;

	void remove(const NodeTypeSet& typeSet);
	NodeTypeSet getWithRemoved(const NodeTypeSet& typeSet) const;

	void keepMatching(const std::function<bool(const NodeType&)>& matcher);
	NodeTypeSet getWithMatchingKept(const std::function<bool(const NodeType&)>& matcher) const;

	void removeMatching(const std::function<bool(const NodeType&)>& matcher);
	NodeTypeSet getWithMatchingRemoved(const std::function<bool(const NodeType&)>& matcher) const;

	bool isEmpty() const;
	bool contains(const NodeType& type) const;
	bool containsMatching(const std::function<bool(const NodeType&)>& matcher) const;
	bool intersectsWith(const NodeTypeSet& typeSet) const;
	std::vector<Id> getNodeTypeIds() const;

private:
	NodeTypeSet(MaskType typeMask);

	MaskType m_nodeTypeMask;
};

// NodeTypeSet is not mutually dependent with any other type, so its inline bodies can be pulled in
// right here -- unguarded -- in both builds (NodeType is complete via the import/include above, and
// the .inl's #pragma once keeps it single even if the module wrapper includes it again later).
#include "NodeTypeSet.inl"

#endif	  // NODE_TYPE_SET_H
