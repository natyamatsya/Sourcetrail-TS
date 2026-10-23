#include "GraphControls.h"

#include <type_traits>

#include "Edge.h"
#include "NodeType.h"

static_assert(std::is_same_v<Edge::TypeMask, decltype(TrailDefaults::edgeTypes)>);

BundleSplit bundleSplitFor(const NodeType& bundledType, const std::string& name)
{
	// TODO: move to language package (as it was in QtGraphNodeBundle).
	const bool listed = !bundledType.isUnknownSymbol() || name == "Symbols";
	return {listed && name != "Anonymous Namespaces", listed};
}

std::optional<TrailDefaults> trailDefaultsFor(const NodeType& type)
{
	if (type.isInheritable())
	{
		return TrailDefaults {Edge::EDGE_INHERITANCE | Edge::EDGE_TEMPLATE_SPECIALIZATION, false};
	}
	if (type.isCallable())
	{
		return TrailDefaults {Edge::EDGE_CALL | Edge::EDGE_OVERRIDE, true};
	}
	if (type.isFile())
	{
		return TrailDefaults {Edge::EDGE_INCLUDE, true};
	}
	return std::nullopt;
}
