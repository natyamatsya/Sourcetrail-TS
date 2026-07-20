// `srctrl.file` -- the file/path leaf cluster: FilePath (a Qt-free, std::filesystem-backed value
// type), FileInfo (path + last-write TimeStamp), TextAccess (line-oriented text file access),
// FileSystem (directory traversal / file ops), FileTree (relative-path root resolution), and the
// application path registries (AppPath / UserPaths / ResourcePaths). Module build only.

module;

// Global module fragment: std + the non-modularized deps. Platform.h (compile-time platform oracle) and
// logging.h (LOG_* macros used by the .inls -- macros don't cross an `import`) stay global-module.
#ifndef SRCTRL_IMPORT_STD
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>
#endif
#include "Platform.h"
#include "logging.h"

export module srctrl.file;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.utility;   // TimeStamp (FileInfo/FileSystem), utilityString (FileSystem)
import srctrl.qt;        // QString/QRegularExpression (FilePathFilter) -- keeps Qt out of this GMF
import srctrl.logging;   // srctrl::log machinery behind the LOG_* macros

#define SRCTRL_MODULE_PURVIEW
#include "FilePath.h"
#include "FilePathFilter.h"
#include "FileInfo.h"
#include "TextAccess.h"
#include "FileSystem.h"
#include "FileTree.h"
#include "AppPath.h"
#include "UserPaths.h"
#include "ResourcePaths.h"
