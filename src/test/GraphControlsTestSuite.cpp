#include "Catch2.hpp"

#include <optional>

#include "Edge.h"
#include "GraphControls.h"
#include "NodeType.h"

TEST_CASE("bundle split keeps the neighbours of unknown symbols")
{
	const BundleSplit functions = bundleSplitFor(NodeType(NODE_FUNCTION), "Referencing Symbols");
	REQUIRE(functions.removeOtherNodes);
	REQUIRE(functions.layoutToList);

	const BundleSplit unknown = bundleSplitFor(NodeType(NODE_SYMBOL), "Unresolved");
	REQUIRE_FALSE(unknown.removeOtherNodes);
	REQUIRE_FALSE(unknown.layoutToList);

	const BundleSplit anonymous = bundleSplitFor(NodeType(NODE_NAMESPACE), "Anonymous Namespaces");
	REQUIRE_FALSE(anonymous.removeOtherNodes);
	REQUIRE(anonymous.layoutToList);
}

TEST_CASE("trail buttons follow the edges of the node's kind")
{
	const std::optional<TrailDefaults> calls = trailDefaultsFor(NodeType(NODE_FUNCTION));
	REQUIRE(calls);
	REQUIRE(calls->edgeTypes == (Edge::EDGE_CALL | Edge::EDGE_OVERRIDE));
	REQUIRE(calls->horizontal);

	const std::optional<TrailDefaults> inheritance = trailDefaultsFor(NodeType(NODE_CLASS));
	REQUIRE(inheritance);
	REQUIRE(inheritance->edgeTypes == (Edge::EDGE_INHERITANCE | Edge::EDGE_TEMPLATE_SPECIALIZATION));
	REQUIRE_FALSE(inheritance->horizontal);	// top down

	const std::optional<TrailDefaults> includes = trailDefaultsFor(NodeType(NODE_FILE));
	REQUIRE(includes);
	REQUIRE(includes->edgeTypes == Edge::EDGE_INCLUDE);

	REQUIRE_FALSE(trailDefaultsFor(NodeType(NODE_FIELD)));
}
