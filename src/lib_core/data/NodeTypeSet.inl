// Inline member definitions for NodeTypeSet.h (included at the end of that header). All members are
// inline because an out-of-line member of an exported class does not resolve for module importers.

#pragma once

namespace
{

inline std::vector<NodeType> getAllNodeTypes()
{
	static const std::vector<NodeType> s_allNodeTypes = {
		NodeType(NODE_SYMBOL),
		NodeType(NODE_TYPE),
		NodeType(NODE_BUILTIN_TYPE),
		NodeType(NODE_MODULE),
		NodeType(NODE_NAMESPACE),
		NodeType(NODE_PACKAGE),
		NodeType(NODE_STRUCT),
		NodeType(NODE_CLASS),
		NodeType(NODE_INTERFACE),
		NodeType(NODE_GLOBAL_VARIABLE),
		NodeType(NODE_FIELD),
		NodeType(NODE_FUNCTION),
		NodeType(NODE_METHOD),
		NodeType(NODE_ENUM),
		NodeType(NODE_ENUM_CONSTANT),
		NodeType(NODE_TYPEDEF),
		NodeType(NODE_TYPE_PARAMETER),
		NodeType(NODE_FILE),
		NodeType(NODE_MACRO),
		NodeType(NODE_UNION),
		NodeType(NODE_RECORD),
		NodeType(NODE_CONCEPT)
	};
	return s_allNodeTypes;
}

inline NodeTypeSet::MaskType nodeTypeToMask(const NodeType& nodeType)
{
	// TODO: convert to mask if ids are not power of two anymore
	return static_cast<NodeTypeSet::MaskType>(static_cast<Id::type>(nodeType.getId()));
}

}

inline NodeTypeSet NodeTypeSet::all()
{
	NodeTypeSet ret;
	ret.m_nodeTypeMask = ~0;
	return ret;
}

inline NodeTypeSet NodeTypeSet::none()
{
	return NodeTypeSet();
}

inline NodeTypeSet::NodeTypeSet()
{
	m_nodeTypeMask = 0;
}

inline NodeTypeSet::NodeTypeSet(MaskType typeMask)
{
	m_nodeTypeMask = typeMask;
}

inline NodeTypeSet::NodeTypeSet(const NodeType& type)
: m_nodeTypeMask(nodeTypeToMask(type))
{
}

inline bool NodeTypeSet::operator==(const NodeTypeSet& other) const
{
	return m_nodeTypeMask == other.m_nodeTypeMask;
}

inline bool NodeTypeSet::operator!=(const NodeTypeSet& other) const
{
	return !operator==(other);
}

inline std::vector<NodeType> NodeTypeSet::getNodeTypes() const
{
	std::vector<NodeType> nodeTypes;

	for (const NodeType& type: getAllNodeTypes())
	{
		if (m_nodeTypeMask & nodeTypeToMask(type))
		{
			nodeTypes.push_back(type);
		}
	}

	return nodeTypes;
}

inline void NodeTypeSet::invert()
{
	m_nodeTypeMask = ~m_nodeTypeMask;
}

inline NodeTypeSet NodeTypeSet::getInverse() const
{
	NodeTypeSet ret(*this);
	ret.invert();
	return ret;
}

inline void NodeTypeSet::add(const NodeTypeSet& typeSet)
{
	m_nodeTypeMask |= typeSet.m_nodeTypeMask;
}

inline NodeTypeSet NodeTypeSet::getWithAdded(const NodeTypeSet& typeSet) const
{
	return NodeTypeSet(m_nodeTypeMask | typeSet.m_nodeTypeMask);
}

inline void NodeTypeSet::remove(const NodeTypeSet& typeSet)
{
	m_nodeTypeMask &= ~typeSet.m_nodeTypeMask;
}

inline NodeTypeSet NodeTypeSet::getWithRemoved(const NodeTypeSet& typeSet) const
{
	return NodeTypeSet(m_nodeTypeMask & ~typeSet.m_nodeTypeMask);
}

inline void NodeTypeSet::keepMatching(const std::function<bool(const NodeType&)>& matcher)
{
	for (const NodeType& type: getAllNodeTypes())
	{
		if (m_nodeTypeMask & nodeTypeToMask(type) && !matcher(type))
		{
			remove(type);
		}
	}
}

inline NodeTypeSet NodeTypeSet::getWithMatchingKept(const std::function<bool(const NodeType&)>& matcher) const
{
	NodeTypeSet ret(*this);
	ret.keepMatching(matcher);
	return ret;
}

inline void NodeTypeSet::removeMatching(const std::function<bool(const NodeType&)>& matcher)
{
	for (const NodeType& type: getAllNodeTypes())
	{
		if (m_nodeTypeMask & nodeTypeToMask(type) && matcher(type))
		{
			remove(type);
		}
	}
}

inline NodeTypeSet NodeTypeSet::getWithMatchingRemoved(const std::function<bool(const NodeType&)>& matcher) const
{
	NodeTypeSet ret(*this);
	ret.removeMatching(matcher);
	return ret;
}

inline bool NodeTypeSet::isEmpty() const
{
	return m_nodeTypeMask == 0;
}

inline bool NodeTypeSet::contains(const NodeType& type) const
{
	return m_nodeTypeMask & nodeTypeToMask(type);
}

inline bool NodeTypeSet::containsMatching(const std::function<bool(const NodeType&)>& matcher) const
{
	for (const NodeType& type: getAllNodeTypes())
	{
		if (m_nodeTypeMask & nodeTypeToMask(type) && matcher(type))
		{
			return true;
		}
	}
	return false;
}

inline bool NodeTypeSet::intersectsWith(const NodeTypeSet& typeSet) const
{
	return m_nodeTypeMask & typeSet.m_nodeTypeMask;
}

inline std::vector<Id> NodeTypeSet::getNodeTypeIds() const
{
	std::vector<Id> ids;

	for (const NodeType& type: getAllNodeTypes())
	{
		if (m_nodeTypeMask & nodeTypeToMask(type))
		{
			ids.push_back(type.getId());
		}
	}

	return ids;
}
