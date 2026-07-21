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
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
#include "StorageAccess.h"

// close the purview-wide extern "C++" linkage block
}
