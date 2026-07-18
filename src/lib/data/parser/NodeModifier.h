#ifndef NODE_MODIFIER_H
#define NODE_MODIFIER_H

#include <string>

// Per-node modifier flags — orthogonal properties a node carries on top of its
// NodeKind, stored as a bitmask (StorageNode::modifiers). Kept separate from
// NodeKind so a modified node still behaves as its underlying kind (an actor is
// still a NODE_CLASS for every consumer that does not care about the modifier).
//
// Persisted to the SQLite DB and the IPC wire as the raw int, so values are
// append-only — never renumber.
enum NodeModifierType
{
	NODE_MODIFIER_NONE = 0,
	// A Swift `actor` (SW13). Modelled as a class + this flag rather than a new
	// node kind.
	NODE_MODIFIER_ACTOR = 1 << 0,
	// A Swift `async` function/initializer (SW13).
	NODE_MODIFIER_ASYNC = 1 << 1,
	// A Swift `nonisolated` member (SW13).
	NODE_MODIFIER_NONISOLATED = 1 << 2,
	// A deprecated declaration (the boolean of a cross-axis fact; the message/
	// since-version rides the node_attribute table under DEPRECATED). Swift
	// `@available(*, deprecated)`, C++ `[[deprecated]]`, Rust `#[deprecated]`.
	NODE_MODIFIER_DEPRECATED = 1 << 3,
};

using NodeModifierMask = int;

inline bool nodeModifierHas(NodeModifierMask modifiers, NodeModifierType flag)
{
	return (modifiers & flag) != 0;
}

// Space-joined labels for the set modifiers ("actor", "async",
// "nonisolated async"), for readable node-kind strings. Empty when none.
std::string nodeModifierToString(NodeModifierMask modifiers);

#endif	  // NODE_MODIFIER_H
