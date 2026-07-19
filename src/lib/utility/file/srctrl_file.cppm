// `srctrl.file` -- FilePath as a standalone leaf module. A Qt-free, std::filesystem-backed value type.
// Module build only.

module;

// Global module fragment: std + the non-modularized deps. Platform.h (compile-time platform oracle) and
// logging.h (LOG_* macros used by FilePath.inl -- macros don't cross an `import`) stay global-module.
#ifndef SRCTRL_IMPORT_STD
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#endif
#include "Platform.h"
#include "logging.h"

export module srctrl.file;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.utility;   // utility::splitToVector (expandEnvironmentVariables)
import srctrl.logging;   // srctrl::log machinery behind the LOG_* macros

#define SRCTRL_MODULE_PURVIEW
#include "FilePath.h"
