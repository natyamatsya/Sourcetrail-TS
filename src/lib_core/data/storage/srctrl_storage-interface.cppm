// `srctrl.storage:interface` partition -- the abstract write interface (Storage) + StorageStats.
// Module build only.
//
// StorageAccess (the read interface) is deliberately NOT here yet: it pulls ErrorInfo.h (-> StorageError)
// and SearchMatch.h (-> Node) into the GMF, which would double those modularized types against the
// imports. It waits until ErrorInfo/SearchMatch are modularized (a read-side/search cluster).

module;

#ifndef SRCTRL_IMPORT_STD
#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#endif

// Non-modularized GMF deps: Id (types.h) and tracing.h's TRACE() macro (still a macro -- with
// tracing off the header is only the empty macros, so it drags no modularized types into the GMF).
#include "types.h"
#include "tracing.h"

export module srctrl.storage:interface;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.utility;   // TimeStamp (StorageStats) -- the :time partition
import srctrl.logging;   // srctrl::log::warning/error in Storage::inject
import :types;           // the storage record structs

#define SRCTRL_MODULE_PURVIEW
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
#include "StorageStats.h"
#include "Storage.h"

// close the purview-wide extern "C++" linkage block
}
