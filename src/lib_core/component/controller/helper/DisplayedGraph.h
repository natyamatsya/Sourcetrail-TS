#ifndef DISPLAYED_GRAPH_H
#define DISPLAYED_GRAPH_H

#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "NodeKind.h"
#include "TabIds.h"
#include "types.h"

class NodeType;
struct DummyNode;

// A node of the graph view as drawn: a symbol, or a bundle or group the graph made.
struct DisplayedGraphNode
{
	enum class Kind
	{
		Node,
		Bundle,
		Group
	};

	Kind kind = Kind::Node;
	Id id = 0;	// the symbol's node id; a bundle's or group's id in this graph
	std::string name;
	NodeKind nodeKind = NODE_SYMBOL;	// a bundle's: the kind of what it bundles
	int parent = -1;	// the index of the node this one is drawn in, -1 for none
	bool active = false;
	// A node with an expand toggle: whether its members show, and how many more
	// expanding it would show.
	bool expandable = false;
	bool expanded = false;
	std::size_t hiddenMembers = 0;
	// A bundle or group: how many nodes it holds, whether it can be split, and how
	// a split lays them out (as clicking it does).
	std::size_t bundled = 0;
	bool splittable = false;
	// A group's kind: "file" and "namespace" groups stand for that node (its id;
	// clicking one activates it), "language" groups are inert.
	std::string group;
	bool splitRemovesOthers = false;
	bool splitToList = false;
};

// How clicking a bundle splits it (QtGraphNodeBundle): the other nodes go and the
// bundled ones are listed, except for bundles of unknown symbols, which keep their
// neighbours (and anonymous namespaces, which are never listed alone).
struct BundleSplit
{
	bool removeOtherNodes = false;
	bool layoutToList = false;
};
BundleSplit bundleSplitFor(const NodeType& bundledType, const std::string& name);

// What each tab's graph view shows, as its GraphController last built it. The
// storage's graph says what exists; this says what is on screen and what the
// graph's controls (expand, hide, split) act on. Remote control reads it.
class DisplayedGraph
{
public:
	static DisplayedGraph& instance();

	// The view of `tab` now shows `nodes` (top level; members and bundled nodes
	// below them).
	void set(TabId tab, const std::vector<std::shared_ptr<DummyNode>>& nodes);
	void clear(TabId tab);
	std::vector<DisplayedGraphNode> get(TabId tab) const;

private:
	mutable std::mutex m_mutex;
	std::map<TabId, std::vector<DisplayedGraphNode>> m_graphs;
};

#endif	  // DISPLAYED_GRAPH_H
