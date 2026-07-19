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

// Non-modularized GMF deps: Id (types.h), TimeStamp (StorageStats), and tracing.h's TRACE() macro
// (still a macro -- textual in the purview, like logging.h was before srctrl.logging).
#include "types.h"
#include "TimeStamp.h"
#include "tracing.h"

export module srctrl.storage:interface;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.logging;   // srctrl::log::warning/error in Storage::inject
import :types;           // the storage record structs

#define SRCTRL_MODULE_PURVIEW
#include "StorageStats.h"
#include "Storage.h"
