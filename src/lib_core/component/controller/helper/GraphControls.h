#ifndef GRAPH_CONTROLS_H
#define GRAPH_CONTROLS_H

#include <optional>
#include <string>

class NodeType;

// How the graph's own controls act, for the graph view and remote control alike.

// How clicking a bundle splits it (QtGraphNodeBundle): the other nodes go and the
// bundled ones are listed, except for bundles of unknown symbols, which keep their
// neighbours (and anonymous namespaces, which are never listed alone).
struct BundleSplit
{
	bool removeOtherNodes = false;
	bool layoutToList = false;
};
BundleSplit bundleSplitFor(const NodeType& bundledType, const std::string& name);

// The trail the graph's trail buttons show for a node of a type: the edges they
// follow, and whether it is laid out left to right (inheritance runs top down).
struct TrailDefaults
{
	int edgeTypes = 0;	  // an Edge::TypeMask
	bool horizontal = true;
};
// None for a node the buttons show no trail for.
std::optional<TrailDefaults> trailDefaultsFor(const NodeType& type);

#endif	  // GRAPH_CONTROLS_H
