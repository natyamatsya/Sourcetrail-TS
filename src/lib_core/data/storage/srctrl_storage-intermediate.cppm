// `srctrl.storage:intermediate` partition -- IntermediateStorage, the in-memory Storage the indexer
// pipeline fills and the interprocess layer serializes. Module build only.
//
// This is the S4 slice with forward value: Phase 4 (the indexer binary as a pure module consumer)
// needs IntermediateStorage importable. The app-side god-object layer (PersistentStorage +
// StorageAccessProxy/Cache/Provider, ~6.5k LOC of impl consumed only by classic TUs) deliberately
// stays classic -- see DESIGN_STORAGE_MODULARIZATION.md §5.

module;

#ifndef SRCTRL_IMPORT_STD
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#endif

// Non-modularized GMF deps: Id (types.h) and tracing.h's TRACE() macro (Storage.h's inline inject()
// uses it; with tracing off the header is only the empty macros).
#include "types.h"
#include "tracing.h"

export module srctrl.storage:intermediate;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.utility;   // TimeStamp (StorageStats via :interface)
import srctrl.logging;   // srctrl::log in Storage::inject
import srctrl.data;      // NodeKind, NodeModifierMask, LocationType (:types)
import :types;           // the storage record structs
import :interface;       // Storage (base class) + StorageStats

#define SRCTRL_MODULE_PURVIEW
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
#include "IntermediateStorage.h"

// close the purview-wide extern "C++" linkage block
}
