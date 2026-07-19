#ifndef NODE_ATTRIBUTE_KIND_H
#define NODE_ATTRIBUTE_KIND_H

#include "SrctrlModule.h"

// utilityEnum (intToEnum / lookupEnum) lives in srctrl.utility. In the header build we #include it; in
// the module build the wrapper `import srctrl.utility;`s before pulling us into the purview.
#ifndef SRCTRL_MODULE_PURVIEW
#include "utilityEnum.h"

#include <string>
#endif

// The key of a sparse, display-only node_attribute(node_id, key, value) row —
// the "open-ended / sparse" facet shape from context/DESIGN_STORAGE_CODEGEN.md.
// Facts that are neither a fixed enum/bitmask (those are inline columns) nor a
// navigable relation (those are edges) live here: platform availability,
// deprecation text, config guards, doc summaries.
//
// Persisted to the SQLite DB and the IPC wire as the raw int, so values are
// append-only — never renumber.
SRCTRL_EXPORT enum class NodeAttributeKind
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

// Explicit specialization of the imported `intToEnum` template (declared+defined inline in the .inl).
template <>
NodeAttributeKind intToEnum(int value);

SRCTRL_EXPORT std::string nodeAttributeKindToString(NodeAttributeKind t);

#include "NodeAttributeKind.inl"

#endif	  // NODE_ATTRIBUTE_KIND_H
