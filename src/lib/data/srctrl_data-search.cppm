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
#include "SearchMatch.h"
