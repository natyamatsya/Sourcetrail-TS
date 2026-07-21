// `srctrl.cxx:package` partition -- the language-package glue that plugs lib_cxx into the app and
// the indexer subprocess: LanguagePackageCxx (the LanguagePackage implementation instantiating
// IndexerCxx), registerCxxIndexerCommandCodec (the Cxx codec for the interprocess registry), and the
// two crash-isolated prebuild runners (CxxModulePrebuildRunner / CxxPchBuildRunner). The one
// srctrl.cxx partition that imports srctrl.interprocess (the codec registry + IpcSerializer live
// there), which keeps that dependency out of the parser-pipeline partitions. Module build only.

module;

#ifndef SRCTRL_IMPORT_STD
#include <algorithm>
#include <cstdlib>
#include <deque>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#endif

#include <nlohmann/json.hpp>

#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/TokenKinds.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Lex/Lexer.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/raw_ostream.h>

// The Cxx codec speaks the flatbuffers wire form directly (Sourcetrail::Ipc::IndexerCommand).
#include <flatbuffers/flatbuffers.h>
#include "indexer_command_generated.h"

// Non-modularized GMF deps: the logging macros' backend seam and ToolChain (std-only header,
// classic impl).
#include "ToolChain.h"

export module srctrl.cxx:package;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.utility;      // utility::concat / append (:containers)
import srctrl.process;      // utility::resolveCompilerResourceDir
import srctrl.file;         // FilePath, FilePathFilter, FileRegister, FileSystem, ResourcePaths
import srctrl.storage;      // IntermediateStorage (the runners' throwaway recording sink)
import srctrl.indexer;      // LanguagePackage, IndexerBase, ParserClientImpl, IndexerCommand
import srctrl.interprocess; // IndexerCommandCodec(+Registry), IpcSerializer
import srctrl.logging;      // LogManager behind the LOG_* macro expansions
import :tooling;            // IndexerCommandCxx
import :parser;             // CanonicalFilePathCache
import :frontend;           // CxxParser, CxxCompilationDatabaseSingle, CxxDiagnosticConsumer,
                            // GeneratePCHAction, SingleFrontendActionFactory, IndexerCxx

#define SRCTRL_MODULE_PURVIEW
// LOG_* macro definitions only (in the purview the header strips its backend includes); the
// expansions name the LogManager imported from srctrl.logging.
#include "logging.h"
// Class definitions, then the inline bodies (each header's own bottom-include is purview-guarded).
#include "LanguagePackageCxx.h"
#include "CxxIndexerCommandCodec.h"
#include "CxxModulePrebuildRunner.h"
#include "CxxPchBuildRunner.h"

#include "LanguagePackageCxx.inl"
#include "CxxIndexerCommandCodec.inl"
#include "CxxModulePrebuildRunner.inl"
#include "CxxPchBuildRunner.inl"
