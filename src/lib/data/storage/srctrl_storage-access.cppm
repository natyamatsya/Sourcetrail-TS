// `srctrl.storage:access` partition -- StorageAccess, the abstract read interface. Module build only.
// This is the payoff of the read-side cluster (Vector2/math -> SearchMatch/TooltipInfo -> error types);
// StorageAccess pulls all of them, so they had to modularize first.

module;

#ifndef SRCTRL_IMPORT_STD
#include <map>
#include <memory>
#include <string>
#include <vector>
#endif

// GMF: Id (types.h), the bookmark records, and StorageAccess's non-modularized forward-decl types
// (FilePath / NodeTypeSet / TextAccess / FileInfo -- none pull a modularized header). The module-typed
// forward decls (Graph, SourceLocationFile/Collection) come from `import srctrl.data` instead.
#include "types.h"
#include "BookmarkCategory.h"
#include "EdgeBookmark.h"
#include "NodeBookmark.h"
#include "FilePath.h"
#include "TextAccess.h"
#include "FileInfo.h"

export module srctrl.storage:access;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.data;   // Node, LocationType, TooltipOrigin, SearchMatch, TooltipInfo, Graph, SourceLocation*
import :types;        // StorageEdge, StorageError
import :error;        // ErrorInfo, ErrorCountInfo, ErrorFilter
import :interface;    // StorageStats

#define SRCTRL_MODULE_PURVIEW
#include "StorageAccess.h"
