// `srctrl.data:location` partition -- SourceLocation, SourceLocationFile, SourceLocationCollection.
// Module build only.

module;

// Global module fragment: std + the non-modularized deps (Id via types.h, and logging.h's
// LOG_ERROR used by SourceLocationCollection) stay global-module. FilePath comes from
// `import srctrl.file`, NOT the GMF.
#ifndef SRCTRL_IMPORT_STD
#include <functional>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <vector>
#endif
#include "types.h"

export module srctrl.data:location;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.file;      // FilePath
import srctrl.logging;   // LogManager behind the LOG_* macro expansions
import :types;   // LocationType

#define SRCTRL_MODULE_PURVIEW
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
// LOG_* macro definitions only (in the purview the header strips its backend includes); the
// expansions name the LogManager imported from srctrl.logging.
#include "logging.h"
// Class definitions first: SourceLocation and SourceLocationFile are mutually dependent, so both must
// be complete before either .inl (whose inline members dereference the partner).
#include "SourceLocation.h"
#include "SourceLocationFile.h"
#include "SourceLocationCollection.h"
#include "ParseLocation.h"
// Then the inline member definitions.
#include "SourceLocation.inl"
#include "SourceLocationFile.inl"
#include "SourceLocationCollection.inl"
// (ParseLocation.inl is pulled by its header unconditionally)

// close the purview-wide extern "C++" linkage block
}
