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
// ErrorInfo first -- ErrorCountInfo and ErrorFilter include it.
#include "ErrorInfo.h"
#include "ErrorCountInfo.h"
#include "ErrorFilter.h"
