// Inline member definitions for Graph.h (included at the end of that header). All members are inline
// because an out-of-line member of an exported class does not resolve for module importers.

#pragma once

inline Graph::Graph() = default;

inline Graph::~Graph()
{
	m_edges.clear();
	m_nodes.clear();
}

inline void Graph::clear()
{
	m_edges.clear();
	m_nodes.clear();
}

inline void Graph::forEachNode(std::function<void(Node*)> func) const
{
	for (const auto &node: m_nodes)
	{
		func(node.second.get());
	}
}

inline void Graph::forEachEdge(std::function<void(Edge*)> func) const
{
	for (const auto &edge: m_edges)
	{
		func(edge.second.get());
	}
}

inline void Graph::forEachToken(std::function<void(Token*)> func) const
{
	forEachNode(func);
	forEachEdge(func);
}

inline Node* Graph::createNode(Id id, NodeType type, NameHierarchy nameHierarchy, DefinitionKind definitionKind)
{
	Node* n = getNodeById(id);
	if (n)
	{
		return n;
	}

	std::shared_ptr<Node> node = std::make_shared<Node>(
		id, type, std::move(nameHierarchy), definitionKind);
	m_nodes.emplace(node->getId(), node);
	return node.get();
}

inline Edge* Graph::createEdge(Id id, Edge::EdgeType type, Node* from, Node* to)
{
	Edge* e = getEdgeById(id);
	if (e)
	{
		return e;
	}

	if (!getNodeById(from->getId()) || !getNodeById(to->getId()))
	{
		srctrl::log::error("Can't add edge, without adding the nodes first.");
		return nullptr;
	}

	std::shared_ptr<Edge> edge = std::make_shared<Edge>(id, type, from, to);
	m_edges.emplace(edge->getId(), edge);
	return edge.get();
}

inline std::size_t Graph::getNodeCount() const
{
	return m_nodes.size();
}

inline std::size_t Graph::getEdgeCount() const
{
	return m_edges.size();
}

inline Node* Graph::getNodeById(Id id) const
{
	std::map<Id, std::shared_ptr<Node>>::const_iterator it = m_nodes.find(id);
	if (it != m_nodes.end())
	{
		return it->second.get();
	}
	return nullptr;
}

inline Edge* Graph::getEdgeById(Id id) const
{
	std::map<Id, std::shared_ptr<Edge>>::const_iterator it = m_edges.find(id);
	if (it != m_edges.end())
	{
		return it->second.get();
	}
	return nullptr;
}

inline const std::map<Id, std::shared_ptr<Node>>& Graph::getNodes() const
{
	return m_nodes;
}

inline const std::map<Id, std::shared_ptr<Edge>>& Graph::getEdges() const
{
	return m_edges;
}

inline void Graph::removeNode(Node* node)
{
	std::map<Id, std::shared_ptr<Node>>::const_iterator it = m_nodes.find(node->getId());
	if (it == m_nodes.end())
	{
		srctrl::log::warning("Node was not found in the graph.");
		return;
	}

	std::vector<Node*> childNodesToRemove;
	node->forEachEdgeOfType(Edge::EDGE_MEMBER, [node, &childNodesToRemove](Edge* e) {
		if (node == e->getFrom())
		{
			childNodesToRemove.push_back(e->getTo());
		}
	});

	for (Node* childNode: childNodesToRemove)
	{
		removeNode(childNode);
	}

	std::vector<Edge*> edgesToRemove;
	node->forEachEdge([&edgesToRemove](Edge* e) { edgesToRemove.push_back(e); });

	for (Edge* edge: edgesToRemove)
	{
		removeEdgeInternal(edge);
	}

	if (node->getEdgeCount())
	{
		srctrl::log::error("Node still has edges.");
	}

	m_nodes.erase(it);
}

inline void Graph::removeEdge(Edge* edge)
{
	std::map<Id, std::shared_ptr<Edge>>::const_iterator it = m_edges.find(edge->getId());
	if (it == m_edges.end())
	{
		srctrl::log::warning("Edge was not found in the graph.");
	}

	if (edge->getType() == Edge::EDGE_MEMBER)
	{
		srctrl::log::error("Can't remove member edge, without removing the child node.");
		return;
	}

	m_edges.erase(it);
}

inline Node* Graph::findNode(std::function<bool(Node*)> func) const
{
	std::map<Id, std::shared_ptr<Node>>::const_iterator it = find_if(
		m_nodes.begin(), m_nodes.end(), [&func](const std::pair<Id, std::shared_ptr<Node>>& n) {
			return func(n.second.get());
		});

	if (it != m_nodes.end())
	{
		return it->second.get();
	}

	return nullptr;
}

inline Edge* Graph::findEdge(std::function<bool(Edge*)> func) const
{
	std::map<Id, std::shared_ptr<Edge>>::const_iterator it = find_if(
		m_edges.begin(), m_edges.end(), [func](const std::pair<Id, std::shared_ptr<Edge>>& e) {
			return func(e.second.get());
		});

	if (it != m_edges.end())
	{
		return it->second.get();
	}

	return nullptr;
}

inline Token* Graph::findToken(std::function<bool(Token*)> func) const
{
	Node* node = findNode(func);
	if (node)
	{
		return node;
	}

	Edge* edge = findEdge(func);
	if (edge)
	{
		return edge;
	}

	return nullptr;
}

inline Node* Graph::addNodeAsPlainCopy(Node* node)
{
	Node* n = getNodeById(node->getId());
	if (n)
	{
		return n;
	}

	std::shared_ptr<Node> copy = std::make_shared<Node>(*node);
	m_nodes.emplace(copy->getId(), copy);
	return copy.get();
}

inline Edge* Graph::addEdgeAsPlainCopy(Edge* edge)
{
	Edge* e = getEdgeById(edge->getId());
	if (e)
	{
		return e;
	}

	Node* from = addNodeAsPlainCopy(edge->getFrom());
	Node* to = addNodeAsPlainCopy(edge->getTo());

	std::shared_ptr<Edge> copy = std::make_shared<Edge>(*edge, from, to);
	m_edges.emplace(copy->getId(), copy);
	return copy.get();
}

inline Node* Graph::addNodeAndAllChildrenAsPlainCopy(Node* node)
{
	Node* n = addNodeAsPlainCopy(node);

	node->forEachEdgeOfType(Edge::EDGE_MEMBER, [node, this](Edge* edge) {
		if (edge->getFrom() == node)
		{
			addEdgeAsPlainCopy(edge);
			addNodeAndAllChildrenAsPlainCopy(edge->getTo());
		}
	});

	return n;
}

inline Edge* Graph::addEdgeAndAllChildrenAsPlainCopy(Edge* edge)
{
	addNodeAndAllChildrenAsPlainCopy(edge->getFrom()->getLastParentNode());
	addNodeAndAllChildrenAsPlainCopy(edge->getTo()->getLastParentNode());

	return addEdgeAsPlainCopy(edge);
}

inline std::size_t Graph::size() const
{
	return getNodeCount() + getEdgeCount();
}

inline Token* Graph::getTokenById(Id id) const
{
	Token* token = getNodeById(id);
	if (!token)
	{
		token = getEdgeById(id);
	}
	return token;
}

inline Graph::TrailMode Graph::getTrailMode() const
{
	return m_trailMode;
}

inline void Graph::setTrailMode(TrailMode trailMode)
{
	m_trailMode = trailMode;
}

inline bool Graph::hasTrailOrigin() const
{
	return m_hasTrailOrigin;
}

inline void Graph::setHasTrailOrigin(bool hasOrigin)
{
	m_hasTrailOrigin = hasOrigin;
}

inline void Graph::print(std::ostream& ostream) const
{
	ostream << "Graph:\n";
	ostream << "nodes (" << getNodeCount() << ")\n";
	forEachNode([&ostream](Node* n) { ostream << *n << '\n'; });

	ostream << "edges (" << getEdgeCount() << ")\n";
	forEachEdge([&ostream](Edge* e) { ostream << *e << '\n'; });
}

inline void Graph::printBasic(std::ostream& ostream) const
{
	ostream << getNodeCount() << " nodes:";
	forEachNode([&ostream](Node* n) {
		ostream << ' ' << n->getReadableTypeString() << ':' << n->getFullName();
	});
	ostream << '\n';

	ostream << getEdgeCount() << " edges:";
	forEachEdge([&ostream](Edge* e) { ostream << ' ' << e->getName(); });
	ostream << '\n';
}

inline void Graph::removeEdgeInternal(Edge* edge)
{
	std::map<Id, std::shared_ptr<Edge>>::const_iterator it = m_edges.find(edge->getId());
	if (it != m_edges.end() && it->second.get() == edge)
	{
		m_edges.erase(it);
	}
}

inline std::ostream& operator<<(std::ostream& ostream, const Graph& graph)
{
	graph.print(ostream);
	return ostream;
}
