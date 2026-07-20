// `srctrl.cxx:tooling` partition -- the Clang-tooling-facing cluster: utility::CompilationDatabase
// (header-path extraction from a JSON compilation database) and IndexerCommandCxx (the Cxx
// indexer-command payload + its Clang-backed static helpers). Module build only.
//
// Per the :context BMI measurement (see DESIGN_INDEXER_MODULARIZATION.md): Clang-bearing code
// clusters into FEW partitions -- each Clang GMF costs ~40 MB of full BMI, so future Clang-tooling
// conversions join THIS partition instead of opening their own.

module;

#ifndef SRCTRL_IMPORT_STD
#include <cstddef>
#include <expected>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#endif

// Non-modularized GMF deps: the Clang tooling headers, ToolChain (std-only header; its impl stays a
// classic TU -- calling global-module functions from a module TU links fine), and the logging macros.
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <llvm/Support/VirtualFileSystem.h>

#include "MessageStatus.h"
#include "Platform.h"
#include "ToolChain.h"

export module srctrl.cxx:tooling;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.utility;   // utility.h helpers (:containers), utilityString (:string), OrderedCache (:cache)
import srctrl.file;      // FilePath, FilePathFilter
import srctrl.process;   // utility::executeProcess (macOS sysroot detection)
import srctrl.indexer;   // IndexerCommandType (was a GMF include until srctrl.indexer owned it)
import srctrl.logging;   // srctrl::log machinery behind the LOG_* macros

#define SRCTRL_MODULE_PURVIEW
// LOG_* macro definitions only (in the purview the header strips its backend includes); the
// expansions name the LogManager imported from srctrl.logging.
#include "logging.h"
#include "CdbLoad.h"
#include "CompilationDatabase.h"
#include "IndexerCommandCxx.h"
