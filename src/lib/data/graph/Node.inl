// Inline member definitions for Node.h (included at the end of that header, after both Node and Edge
// are complete). All members are inline because an out-of-line member of an exported class does not
// resolve for module importers.

#pragma once

inline Node::Node(Id id, NodeType type, NameHierarchy nameHierarchy, DefinitionKind definitionKind)
	: Token(id)
	, m_type(type)
	, m_nameHierarchy(std::move(nameHierarchy))
	, m_definitionKind(definitionKind)

{
}

inline Node::Node(const Node& other)
	: Token(other)
	, m_type(other.m_type)
	, m_nameHierarchy(other.m_nameHierarchy)
	, m_definitionKind(other.m_definitionKind)
	, m_childCount(other.m_childCount)
{
}

inline Node::~Node() = default;

inline NodeType Node::getType() const
{
	return m_type;
}

inline void Node::setType(NodeType type)
{
	if (!isType(type.getKind() | NODE_SYMBOL))
	{
		srctrl::log::warning(
			"Cannot change NodeType after it was already set from " + getReadableTypeString() +
			" to " + type.getReadableTypeString());
		return;
	}
	m_type = type;
}

inline bool Node::isType(NodeKindMask mask) const
{
	return (m_type.getKind() & mask) > 0;
}

inline NodeModifierMask Node::getModifiers() const
{
	return m_modifiers;
}

inline void Node::setModifiers(NodeModifierMask modifiers)
{
	m_modifiers = modifiers;
}

inline bool Node::isActor() const
{
	return nodeModifierHas(m_modifiers, NODE_MODIFIER_ACTOR);
}

inline bool Node::isDeprecated() const
{
	return nodeModifierHas(m_modifiers, NODE_MODIFIER_DEPRECATED);
}

inline std::string Node::getName() const
{
	return m_nameHierarchy.getRawName();
}

inline std::string Node::getFullName() const
{
	return m_nameHierarchy.getQualifiedName();
}

inline const NameHierarchy& Node::getNameHierarchy() const
{
	return m_nameHierarchy;
}

inline bool Node::isDefined() const
{
	return m_definitionKind != DefinitionKind::NONE;
}

inline bool Node::isImplicit() const
{
	return m_definitionKind == DefinitionKind::IMPLICIT;
}

inline bool Node::isExplicit() const
{
	return m_definitionKind == DefinitionKind::EXPLICIT;
}

inline size_t Node::getChildCount() const
{
	return m_childCount;
}

inline void Node::setChildCount(size_t childCount)
{
	m_childCount = childCount;
}

inline size_t Node::getEdgeCount() const
{
	return m_edges.size();
}

inline void Node::addEdge(Edge* edge)
{
	m_edges.emplace(edge->getId(), edge);
}

inline void Node::removeEdge(Edge* edge)
{
	auto it = m_edges.find(edge->getId());
	if (it != m_edges.end())
	{
		m_edges.erase(it);
	}
}

inline Node* Node::getParentNode() const
{
	Edge* edge = getMemberEdge();
	if (edge)
	{
		return edge->getFrom();
	}
	return nullptr;
}

inline Node* Node::getLastParentNode()
{
	Node* parent = getParentNode();
	if (parent)
	{
		return parent->getLastParentNode();
	}
	return this;
}

inline Edge* Node::getMemberEdge() const
{
	return findEdgeOfType(Edge::EDGE_MEMBER, [this](Edge* e) { return e->getTo() == this; });
}

inline bool Node::isParentOf(const Node* node) const
{
	while ((node = node->getParentNode()) != nullptr)
	{
		if (node == this)
		{
			return true;
		}
	}
	return false;
}

inline Edge* Node::findEdge(std::function<bool(Edge*)> func) const
{
	auto it = find_if(
		m_edges.begin(), m_edges.end(), [func](std::pair<Id, Edge*> p) { return func(p.second); });

	if (it != m_edges.end())
	{
		return it->second;
	}

	return nullptr;
}

inline Edge* Node::findEdgeOfType(Edge::TypeMask mask) const
{
	return findEdgeOfType(mask, [](Edge*  /*e*/) { return true; });
}

inline Edge* Node::findEdgeOfType(Edge::TypeMask mask, std::function<bool(Edge*)> func) const
{
	auto it = find_if(m_edges.begin(), m_edges.end(), [mask, func](std::pair<Id, Edge*> p) {
		if (p.second->isType(mask))
		{
			return func(p.second);
		}
		return false;
	});

	if (it != m_edges.end())
	{
		return it->second;
	}

	return nullptr;
}

inline Node* Node::findChildNode(std::function<bool(Node*)> func) const
{
	auto it = find_if(m_edges.begin(), m_edges.end(), [&func](std::pair<Id, Edge*> p) {
		if (p.second->getType() == Edge::EDGE_MEMBER)
		{
			return func(p.second->getTo());
		}
		return false;
	});

	if (it != m_edges.end())
	{
		return it->second->getTo();
	}

	return nullptr;
}

inline void Node::forEachEdge(std::function<void(Edge*)> func) const
{
	for_each(m_edges.begin(), m_edges.end(), [func](std::pair<Id, Edge*> p) { func(p.second); });
}

inline void Node::forEachEdgeOfType(Edge::TypeMask mask, std::function<void(Edge*)> func) const
{
	for_each(m_edges.begin(), m_edges.end(), [mask, func](std::pair<Id, Edge*> p) {
		if (p.second->isType(mask))
		{
			func(p.second);
		}
	});
}

inline void Node::forEachChildNode(std::function<void(Node*)> func) const
{
	forEachEdgeOfType(Edge::EDGE_MEMBER, [func, this](Edge* e) {
		if (this != e->getTo())
		{
			func(e->getTo());
		}
	});
}

inline void Node::forEachNodeRecursive(std::function<void(const Node*)> func) const
{
	func(this);

	forEachEdgeOfType(Edge::EDGE_MEMBER, [func, this](Edge* e) {
		if (this != e->getTo())
		{
			e->getTo()->forEachNodeRecursive(func);
		}
	});
}

inline bool Node::isNode() const
{
	return true;
}

inline bool Node::isEdge() const
{
	return false;
}

inline std::string Node::getReadableTypeString() const
{
	// An actor replaces the kind ("actor"); async/nonisolated qualify it
	// ("async method", "nonisolated async method").
	if (isActor())
	{
		return nodeModifierToString(m_modifiers);
	}
	if (const std::string qualifiers = nodeModifierToString(m_modifiers); !qualifiers.empty())
	{
		return qualifiers + " " + m_type.getReadableTypeString();
	}
	return m_type.getReadableTypeString();
}

inline std::string Node::getAsString() const
{
	std::stringstream str;
	str << "[" << getId() << "] " << getReadableTypeString() << ": " << "\"" << getName()
		<< "\"";

	TokenComponentAccess* access = getComponent<TokenComponentAccess>();
	if (access)
	{
		str << " " << access->getAccessString();
	}

	if (getComponent<TokenComponentStatic>())
	{
		str << " static";
	}

	if (getComponent<TokenComponentConst>())
	{
		str << " const";
	}

	return str.str();
}

inline std::ostream& operator<<(std::ostream& ostream, const Node& node)
{
	ostream << node.getAsString();
	return ostream;
}
