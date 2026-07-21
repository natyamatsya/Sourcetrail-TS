// `srctrl.data:search` partition -- SearchMatch (search result + command). Module build only.

module;

#ifndef SRCTRL_IMPORT_STD
#include <ostream>
#include <sstream>
#include <string>
#include <vector>
#endif

#include "types.h"

export module srctrl.data:search;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.logging;   // srctrl::log::info in SearchMatch::print
import :graph;           // Node, NodeType
import :name;            // NameHierarchy (Node's interface)
import :types;           // NodeKind / getReadableNodeKindString

#define SRCTRL_MODULE_PURVIEW
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
#include "SearchMatch.h"

// close the purview-wide extern "C++" linkage block
}
