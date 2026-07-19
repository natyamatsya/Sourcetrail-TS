// `srctrl.logging` -- the module-native logging front end. A standalone module (no partitions):
// consumers `import srctrl.logging;` and call srctrl::log::error(...) / errorf(...) / error_lazy(...)
// instead of GMF-including logging.h, so a module purview no longer relies on a macro surviving in its
// global module fragment. Module build only.
//
// The classic LogManager backend (and its Logger/LogMessage/LogManagerImplementation deps) stays a
// global-module dependency in the GMF -- this module only wraps its logInfo/logWarning/logError entry
// points. logging.h's 12 LOG_* macros remain untouched as the classic compat shim for the ~489 existing
// call sites (same backend, independent front end -> no behavior change, no ODR interaction).

module;

#ifndef SRCTRL_IMPORT_STD
#include <format>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>
#endif

#include "LogManager.h"

export module srctrl.logging;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

#define SRCTRL_MODULE_PURVIEW
#include "LogFacade.h"
