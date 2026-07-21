// `srctrl.cxx:frontend` partition -- the Clang frontend glue on top of the visitor: CxxParser (the
// libTooling driver), the frontend actions (ASTAction / GeneratePCHAction / ASTConsumer /
// SingleFrontendActionFactory), the preprocessor hooks (PreprocessorCallbacks / CommentHandler),
// CxxDiagnosticConsumer, the invocation helpers (CxxCompilationDatabaseSingle /
// ClangInvocationInfo), and IndexerCxx (its framework deps import cleanly now that
// srctrl.indexer exists). A clean DAG (unlike the visitor blob), so each header carries its own
// classic bottom-include. Module build only.
//
// Clang-bearing (Frontend/Driver/Tooling GMF); per the :context measurement the frontend layers
// join few such partitions rather than opening new ones.

module;

#ifndef SRCTRL_IMPORT_STD
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#endif

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/Basic/IdentifierTable.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/Version.h>
#include <clang/Driver/Compilation.h>
#include <clang/Driver/Driver.h>
#include <clang/Driver/Util.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Frontend/MultiplexConsumer.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Lex/MacroArgs.h>
#include <clang/Lex/MacroInfo.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Lex/Token.h>
#include <clang/Serialization/ASTWriter.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Option/ArgList.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/VirtualFileSystem.h>
#include <llvm/TargetParser/Host.h>

// Non-modularized GMF deps: the clang version-compat shim, Id/types, the logging macros (+ the
// global-module LogManager backend behind them), IndexerStateInfo (a GMF-safe 10-line plain
// struct), and ToolChain (std-only header, classic impl).
#include "clang_compat/ClangCompat.h"
#include "types.h"
#include "IndexerStateInfo.h"
#include "ToolChain.h"

export module srctrl.cxx:frontend;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.utility;   // utility::concat / toLowerCase / isPrefix / trim
import srctrl.process;   // utility::resolveCompilerResourceDir
import srctrl.file;      // FilePath, FileRegister, TextAccess, ResourcePaths
import srctrl.settings;  // ApplicationSettings (verbose-logging switch)
import srctrl.data;      // Parser + ParserClient (:parser), ParseLocation (:location)
import srctrl.indexer;   // Indexer<T>, ParserClientImpl, IndexerCommand (IndexerCxx's base)
import srctrl.logging;   // srctrl::log machinery behind the LOG_* macros
import :tooling;         // IndexerCommandCxx
import :parser;          // CanonicalFilePathCache, utilityClang
import :visitor;         // CxxAstVisitor (ASTConsumer drives it)

#define SRCTRL_MODULE_PURVIEW
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
// LOG_* macro definitions only (in the purview the header strips its backend includes); the
// expansions name the LogManager imported from srctrl.logging.
#include "logging.h"
// Class definitions in dependency order (CommentHandler before ASTAction, which holds it by
// value; everything before CxxParser), then the inline bodies (each header's own bottom-include
// is purview-guarded).
#include "CxxCompilationDatabaseSingle.h"
#include "ClangInvocationInfo.h"
#include "CommentHandler.h"
#include "PreprocessorCallbacks.h"
#include "CxxDiagnosticConsumer.h"
#include "ASTConsumer.h"
#include "ASTAction.h"
#include "GeneratePCHAction.h"
#include "SingleFrontendActionFactory.h"
#include "CxxParser.h"
#include "IndexerCxx.h"

#include "CxxCompilationDatabaseSingle.inl"
#include "ClangInvocationInfo.inl"
#include "CommentHandler.inl"
#include "PreprocessorCallbacks.inl"
#include "CxxDiagnosticConsumer.inl"
#include "ASTConsumer.inl"
#include "ASTAction.inl"
#include "GeneratePCHAction.inl"
#include "SingleFrontendActionFactory.inl"
#include "CxxParser.inl"
#include "IndexerCxx.inl"

// close the purview-wide extern "C++" linkage block
}
