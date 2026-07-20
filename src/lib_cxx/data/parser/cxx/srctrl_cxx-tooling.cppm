// `srctrl.cxx:tooling` partition -- the Clang-tooling-facing cluster. Starts with
// utility::CompilationDatabase (header-path extraction from a JSON compilation database). Module
// build only.
//
// Per the :context BMI measurement (see DESIGN_INDEXER_MODULARIZATION.md): Clang-bearing code
// clusters into FEW partitions -- each Clang GMF costs ~40 MB of full BMI, so future Clang-tooling
// conversions join THIS partition instead of opening their own.

module;

#ifndef SRCTRL_IMPORT_STD
#include <memory>
#include <set>
#include <string>
#include <vector>
#endif

// Non-modularized GMF deps: the Clang tooling headers, ToolChain (std-only header; its impl stays a
// classic TU -- calling global-module functions from a module TU links fine), and the logging macros.
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/JSONCompilationDatabase.h>

#include "ToolChain.h"
#include "logging.h"

export module srctrl.cxx:tooling;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.utility;   // utility.h helpers (:containers), utilityString (:string)
import srctrl.file;      // FilePath
import srctrl.logging;   // srctrl::log machinery behind the LOG_* macros

#define SRCTRL_MODULE_PURVIEW
#include "CompilationDatabase.h"
