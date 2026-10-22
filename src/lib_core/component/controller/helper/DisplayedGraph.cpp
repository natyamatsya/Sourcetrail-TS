#include "DisplayedGraph.h"

#include "DummyNode.h"
#include "GroupType.h"
#include "NodeType.h"

namespace
{
void collect(const DummyNode& node, int parent, std::vector<DisplayedGraphNode>& nodes)
{
	if (!node.visible)
	{
		return;
	}

	if (node.isAccessNode())
	{
		// "public", "private": a section of the members, drawn inside their parent.
		for (const std::shared_ptr<DummyNode>& member: node.subNodes)
		{
			collect(*member, parent, nodes);
		}
		return;
	}

	DisplayedGraphNode shown;
	shown.id = node.tokenId;
	shown.name = node.name;
	shown.parent = parent;
	shown.active = node.active;
	if (node.isGraphNode())
	{
		shown.kind = DisplayedGraphNode::Kind::Node;
		if (node.data != nullptr)
		{
			shown.nodeKind = node.data->getType().getKind();
		}
		for (const std::shared_ptr<DummyNode>& subNode: node.subNodes)
		{
			if (subNode->isExpandToggleNode())
			{
				shown.expandable = true;
				shown.expanded = subNode->expanded;
				shown.hiddenMembers = subNode->invisibleSubNodeCount;
			}
		}
	}
	else if (node.isBundleNode())
	{
		shown.kind = DisplayedGraphNode::Kind::Bundle;
		shown.nodeKind = node.bundledNodeType.getKind();
		shown.bundled = node.getBundledNodeCount();
		shown.splittable = node.interactive;
		const BundleSplit split = bundleSplitFor(node.bundledNodeType, node.name);
		shown.splitRemovesOthers = split.removeOtherNodes;
		shown.splitToList = split.layoutToList;
	}
	else if (node.isGroupNode())
	{
		shown.kind = DisplayedGraphNode::Kind::Group;
		shown.group = groupTypeToString(node.groupType);
		shown.bundled = node.subNodes.size();
		// QtGraphNodeGroup: file and namespace groups activate their node, language
		// groups do nothing, the others split.
		shown.splittable = node.interactive && node.groupType != GroupType::FILE &&
			node.groupType != GroupType::NAMESPACE && node.groupType != GroupType::LANGUAGE;
	}
	else
	{
		return;	// qualifiers, text, expand toggles: part of their node
	}

	const int index = static_cast<int>(nodes.size());
	nodes.push_back(std::move(shown));
	for (const std::shared_ptr<DummyNode>& subNode: node.subNodes)
	{
		collect(*subNode, index, nodes);
	}
}
}	 // namespace

BundleSplit bundleSplitFor(const NodeType& bundledType, const std::string& name)
{
	// TODO: move to language package (as it was in QtGraphNodeBundle).
	const bool listed = !bundledType.isUnknownSymbol() || name == "Symbols";
	return {listed && name != "Anonymous Namespaces", listed};
}

DisplayedGraph& DisplayedGraph::instance()
{
	static DisplayedGraph graph;
	return graph;
}

void DisplayedGraph::set(TabId tab, const std::vector<std::shared_ptr<DummyNode>>& nodes)
{
	std::vector<DisplayedGraphNode> shown;
	for (const std::shared_ptr<DummyNode>& node: nodes)
	{
		collect(*node, -1, shown);
	}
	const std::lock_guard<std::mutex> lock(m_mutex);
	m_graphs[tab] = std::move(shown);
}

void DisplayedGraph::clear(TabId tab)
{
	const std::lock_guard<std::mutex> lock(m_mutex);
	m_graphs.erase(tab);
}

std::vector<DisplayedGraphNode> DisplayedGraph::get(TabId tab) const
{
	const std::lock_guard<std::mutex> lock(m_mutex);
	const auto it = m_graphs.find(tab);
	return it != m_graphs.end() ? it->second : std::vector<DisplayedGraphNode> {};
}
