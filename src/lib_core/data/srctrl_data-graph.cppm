// `srctrl.data:graph` partition -- the token graph: the TokenComponent polymorphic base + its ~9
// concrete subtypes (the leaf layer), and the interconnected core Token / Edge / Node / Graph plus
// NodeType. Module build only.

module;

// Global module fragment: std + the non-modularized deps (Id via types.h, QtResources for
// NodeType's icon paths) stay global-module. FilePath comes from `import srctrl.file`, NOT the GMF. The modularized deps arrive via the imports below:
// utilityEnum/utilityString/Tree (srctrl.utility), the classification enums AccessKind/DefinitionKind/
// ElementComponentKind/NodeKind/NodeModifier (:types), NameHierarchy (:name), and the logging facade
// (srctrl.logging -- the core's LOG_* calls became srctrl::log::error/warning).
#ifndef SRCTRL_IMPORT_STD
#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <typeinfo>
#include <vector>
#endif

#include "types.h"
#include "QtResources.h"

export module srctrl.data:graph;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.utility;   // utilityEnum (intToEnum for Edge::EdgeType), utilityString, Tree (NodeType)
import srctrl.file;      // FilePath
import srctrl.logging;   // srctrl::log::error/warning (the former LOG_* macros)
import :types;           // AccessKind, DefinitionKind, ElementComponentKind, NodeKind, NodeModifier
import :name;            // NameHierarchy (Node)

#define SRCTRL_MODULE_PURVIEW
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
// ---- Leaf layer: TokenComponent base first, then its subtypes (their guarded #include of
// TokenComponent.h is skipped in the purview). ----
#include "TokenComponent.h"
#include "TokenComponentAbstraction.h"
#include "TokenComponentAccess.h"
#include "TokenComponentBundledEdges.h"
#include "TokenComponentConst.h"
#include "TokenComponentFilePath.h"
#include "TokenComponentStatic.h"
#include "TokenComponentInheritanceChain.h"
#include "TokenComponentIsAmbiguous.h"

// ---- Core: all class DEFINITIONS first, then all .inl bodies (every type complete before any inline
// member is parsed -- the Edge<->Node cycle needs this). NodeType is an independent value type Node
// holds; Token is the base of Edge/Node; Edge forward-declares Node (SRCTRL_EXPORT'd) and Node.h needs
// Edge complete, so Edge.h precedes Node.h. Each header's own guarded .inl include is skipped here. ----
#include "NodeType.h"
#include "NodeTypeSet.h"
#include "Token.h"
#include "Edge.h"
#include "Node.h"
#include "Graph.h"

#include "NodeType.inl"
#include "Token.inl"
#include "Edge.inl"
#include "Node.inl"
#include "Graph.inl"

// close the purview-wide extern "C++" linkage block
}
