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
	// A concept's derived hierarchy is the types that satisfy it, a type's base hierarchy
	// includes the concepts it satisfies: compile-time polymorphism next to the run-time kind.
	if (type.isInheritable())
	{
		return TrailDefaults {
			Edge::EDGE_INHERITANCE | Edge::EDGE_SATISFIES | Edge::EDGE_TEMPLATE_SPECIALIZATION, false};
	}
	if (type.getKind() == NODE_CONCEPT)
	{
		return TrailDefaults {Edge::EDGE_SATISFIES, false};
	}
	if (type.isCallable())
	{
		// The dispatch edges let a callee trail go on from a virtual call into the overrides
		// it may reach, and a caller trail from an override back to the calls through its base.
		return TrailDefaults {Edge::EDGE_CALL | Edge::EDGE_DISPATCH | Edge::EDGE_OVERRIDE, true};
	}
	if (type.isFile())
	{
		return TrailDefaults {Edge::EDGE_INCLUDE, true};
	}
	return std::nullopt;
}
