// `srctrl.storage:error` partition -- ErrorInfo / ErrorCountInfo / ErrorFilter (the error record + its
// count/filter helpers). Header-only structs; module build only. Lives in srctrl.storage because
// ErrorInfo carries a StorageError (:types), which would be a cycle if placed in srctrl.data.

module;

#ifndef SRCTRL_IMPORT_STD
#include <cstddef>
#include <string>
#include <vector>
#endif

#include "types.h"   // Id (ErrorInfo carries element ids)

export module srctrl.storage:error;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import :types;   // StorageError (ErrorInfo derives from / carries it)

#define SRCTRL_MODULE_PURVIEW
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
// ErrorInfo first -- ErrorCountInfo and ErrorFilter include it.
#include "ErrorInfo.h"
#include "ErrorCountInfo.h"
#include "ErrorFilter.h"

// close the purview-wide extern "C++" linkage block
}
