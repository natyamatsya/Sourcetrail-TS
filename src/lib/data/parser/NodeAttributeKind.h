#ifndef NODE_ATTRIBUTE_KIND_H
#define NODE_ATTRIBUTE_KIND_H

#include "utilityEnum.h"

#include <string>

// The key of a sparse, display-only node_attribute(node_id, key, value) row —
// the "open-ended / sparse" facet shape from context/DESIGN_STORAGE_CODEGEN.md.
// Facts that are neither a fixed enum/bitmask (those are inline columns) nor a
// navigable relation (those are edges) live here: platform availability,
// deprecation text, config guards, doc summaries.
//
// Persisted to the SQLite DB and the IPC wire as the raw int, so values are
// append-only — never renumber.
enum class NodeAttributeKind
{
	NONE = 0,
	// Platform/version gating text, e.g. Swift `@available(macOS 14, *)`.
	AVAILABILITY = 1,
	// Deprecation message.
	DEPRECATED = 2,
	// Configuration guard, e.g. Rust `cfg(feature = "x")` / Swift `#if`.
	CFG = 3,
	// One-line documentation summary.
	DOC_BRIEF = 4
};

template <>
NodeAttributeKind intToEnum(int value);

std::string nodeAttributeKindToString(NodeAttributeKind t);

#endif	  // NODE_ATTRIBUTE_KIND_H
