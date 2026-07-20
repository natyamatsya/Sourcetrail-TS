// `srctrl.settings` -- the settings stack: ConfigManager (the flat key/value store with TOML/JSON
// backends), Settings (the file-backed base class), and ApplicationSettings (the app-wide
// singleton). Module build only.
//
// This is the last big mid-layer unblock: ApplicationSettings.h was one of the two remaining
// GMF-poison headers (it textually reaches FilePath.h), gating IncludeProcessing and the settings-
// facing lib_cxx conversions.

module;

#ifndef SRCTRL_IMPORT_STD
#include <cmath>
#include <cstddef>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#endif

// Non-modularized GMF deps: the TOML/JSON backends (header-only third-party), GroupType (std-only
// enum header; classic impl linked), Logger (log-level mask constants), and the logging macros.
#include <glaze/glaze.hpp>
#include <toml++/toml.hpp>

#include "GroupType.h"
#include "Logger.h"
#include "logging.h"

export module srctrl.settings;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.utility;   // utility.h helpers (:containers), Status (:types)
import srctrl.file;      // FilePath, TextAccess, UserPaths/ResourcePaths, utilityFile
import srctrl.logging;   // srctrl::log machinery behind the LOG_* macros

#define SRCTRL_MODULE_PURVIEW
// Dependency order: the store, the base class, the singleton.
#include "ConfigManager.h"
#include "Settings.h"
#include "ApplicationSettings.h"
