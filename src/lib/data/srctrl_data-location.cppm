// `srctrl.data:location` partition -- SourceLocation, SourceLocationFile, SourceLocationCollection.
// Module build only.

module;

// Global module fragment: std + the non-modularized deps (FilePath, Id via types.h, and logging.h's
// LOG_ERROR used by SourceLocationCollection) stay global-module.
#include <functional>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <vector>
#include "types.h"
#include "FilePath.h"
#include "logging.h"

export module srctrl.data:location;

import :types;   // LocationType

#define SRCTRL_MODULE_PURVIEW
// Class definitions first: SourceLocation and SourceLocationFile are mutually dependent, so both must
// be complete before either .inl (whose inline members dereference the partner).
#include "SourceLocation.h"
#include "SourceLocationFile.h"
#include "SourceLocationCollection.h"
// Then the inline member definitions.
#include "SourceLocation.inl"
#include "SourceLocationFile.inl"
#include "SourceLocationCollection.inl"
