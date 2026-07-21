// `srctrl.cxx:parser` partition -- the AST-support layer beneath the visitor: CanonicalFilePathCache
// (FileID -> canonical FilePath), the utilityClang helpers, and the name_resolver/ family
// (CxxNameResolver + Decl/Type/Specifier/TemplateArgument/TemplateParameterString resolvers).
// Module build only. Visitor-layer slice 1; the CxxAstVisitor + components follow.
//
// Clang-bearing (AST GMF); per the :context measurement the visitor layers should join few such
// partitions rather than opening new ones.

module;

#ifndef SRCTRL_IMPORT_STD
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#endif

#include <llvm/ADT/DenseMap.h>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/Basic/FileManager.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Preprocessor.h>

// Non-modularized GMF deps: the clang version-compat shim, ToolChain (std-only header, classic
// impl), Id/types, and the logging macros.
#include "clang_compat/ClangCompat.h"
#include "ToolChain.h"
#include "types.h"

export module srctrl.cxx:parser;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.utility;   // utilityString, ScopedSwitcher, UnorderedCache (:cache)
import srctrl.file;      // FilePath, FileRegister, FilePathFilter
import srctrl.process;   // utility::executeProcess (resolveCompilerResourceDir)
import srctrl.data;      // ParseLocation (:location), AccessKind/SymbolKind/DefinitionKind (:types)
import srctrl.logging;   // srctrl::log machinery behind the LOG_* macros
import :name;            // CxxName / CxxDeclName / CxxTypeName / ... (the resolvers' output types)

#define SRCTRL_MODULE_PURVIEW
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
// LOG_* macro definitions only (in the purview the header strips its backend includes); the
// expansions name the LogManager imported from srctrl.logging.
#include "logging.h"
// Class definitions in dependency order, then the mutually-recursive resolver bodies (each header's
// own .inl include is purview-guarded, so every class is complete before any resolver body parses).
#include "CanonicalFilePathCache.h"
#include "utilityClang.h"
#include "utilityClang.inl"
#include "CanonicalFilePathCache.inl"
#include "CxxNameResolver.h"
#include "CxxTypeNameResolver.h"
#include "CxxDeclNameResolver.h"
#include "CxxSpecifierNameResolver.h"
#include "CxxTemplateArgumentNameResolver.h"
#include "CxxTemplateParameterStringResolver.h"

#include "CxxNameResolver.inl"
#include "CxxTypeNameResolver.inl"
#include "CxxDeclNameResolver.inl"
#include "CxxSpecifierNameResolver.inl"
#include "CxxTemplateArgumentNameResolver.inl"
#include "CxxTemplateParameterStringResolver.inl"

// close the purview-wide extern "C++" linkage block
}
