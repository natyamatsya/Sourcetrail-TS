// `srctrl.view` -- the component/view style layer: GraphViewStyleImpl (the pure interface the Qt
// frontend implements classically) and GraphViewStyle (the all-static style facade). First module
// of the component layer; opened to dissolve the GraphViewStyle.h consumer blocker. Module build
// only.

module;

#ifndef SRCTRL_IMPORT_STD
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>
#endif

// Non-modularized GMF deps, classic impls linked: GroupType (std-only enum) and the generated
// QtResources accessors (free functions -- purview-textual would module-mangle their refs).
#include "GroupType.h"
#include "types.h"   // Id (classic-global)
#include "QtResources.h"

export module srctrl.view;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.utility;    // Vector2 (:math), NodeType helpers (:types)
import srctrl.file;       // FilePath (font-path API; transitive in the classic build)
import srctrl.data;       // Node, AccessKind, NodeType
import srctrl.settings;   // ApplicationSettings, ColorScheme (style loading)
import srctrl.logging;    // backend behind the LOG_* macros

#define SRCTRL_MODULE_PURVIEW
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
// LOG_* macro definitions only (backend imported).
#include "logging.h"
// Interface before facade: GraphViewStyle.h fwd-declares GraphViewStyleImpl, and the fwd decl
// must redeclare the already-exported class.
#include "CodeScrollParams.h"
#include "GraphViewStyleImpl.h"
#include "GraphViewStyle.h"

// close the purview-wide extern "C++" linkage block
}
