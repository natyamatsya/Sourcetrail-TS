// `srctrl.file` -- the file/text leaf cluster: FilePath (a Qt-free, std::filesystem-backed value
// type), FileInfo (path + last-write TimeStamp), and TextAccess (line-oriented text file access).
// Module build only.

module;

// Global module fragment: std + the non-modularized deps. Platform.h (compile-time platform oracle) and
// logging.h (LOG_* macros used by the .inls -- macros don't cross an `import`) stay global-module.
#ifndef SRCTRL_IMPORT_STD
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
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

import srctrl.utility;   // TimeStamp (FileInfo) -- the :time partition
import srctrl.logging;   // srctrl::log machinery behind the LOG_* macros

#define SRCTRL_MODULE_PURVIEW
#include "FilePath.h"
#include "FileInfo.h"
#include "TextAccess.h"
