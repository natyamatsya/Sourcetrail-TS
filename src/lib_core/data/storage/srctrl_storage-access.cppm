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

// GMF: Id (types.h). Everything else is imported: FilePath/TextAccess/FileInfo from srctrl.file,
// the module-typed forward decls (Graph, SourceLocationFile/Collection) and the bookmark data
// types from srctrl.data.
#include "types.h"

export module srctrl.storage:access;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.file;   // FilePath, TextAccess, FileInfo
import srctrl.data;   // Node, LocationType, TooltipOrigin, SearchMatch, TooltipInfo, Graph, SourceLocation*, :bookmark
import :types;        // StorageEdge, StorageError
import :error;        // ErrorInfo, ErrorCountInfo, ErrorFilter
import :interface;    // StorageStats

#define SRCTRL_MODULE_PURVIEW
#include "StorageAccess.h"
