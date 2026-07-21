// `srctrl.cxx:visitor` partition -- the visitor blob: CxxAstVisitor (the CRTP RecursiveASTVisitor)
// with its component tuple, the mid-level indexing layer (CxxIndexingContext, CxxSymbolRegistry,
// CxxLocationExtractor), and the concept-/destructor-call recorders. One partition for the whole
// strongly-connected family (see CxxAstVisitorBodies.h for the classic-build story).
// Module build only. Visitor-layer slice 2; the frontend glue (CxxParser/IndexerCxx/actions)
// follows.
//
// Clang-bearing (AST GMF); per the :context measurement the visitor layers join few such
// partitions rather than opening new ones.

module;

#ifndef SRCTRL_IMPORT_STD
#include <concepts>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>
#endif

#pragma warning(push)
#pragma warning(disable: 4702) // unreachable code
#include <clang/AST/RecursiveASTVisitor.h>
#pragma warning(pop)

#include <clang/AST/ASTConcept.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Type.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Analysis/CFG.h>
#include <clang/Basic/Module.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/Preprocessor.h>

// Non-modularized GMF deps: the clang version-compat shim, Id/types, the logging macros, and
// IndexerStateInfo (a GMF-safe 10-line plain struct).
#include "clang_compat/ClangCompat.h"
#include "types.h"
#include "IndexerStateInfo.h"

export module srctrl.cxx:visitor;

#ifdef SRCTRL_IMPORT_STD
import std;
#endif

import srctrl.utility;   // ScopedSwitcher
import srctrl.file;      // FilePath
import srctrl.data;      // ParserClient (:parser), ParseLocation (:location), NameHierarchy +
                         // the main-function helpers (:name),
                         // AccessKind/DefinitionKind/SymbolKind/ReferenceKind/NodeAttributeKind/
                         // NodeModifier (:types)
import srctrl.logging;   // srctrl::log machinery behind the LOG_* macros
import :name;            // CxxName / CxxDeclName / CxxTypeName
import :context;         // CxxContext (the PointerUnion alias over decl/type)
import :parser;          // CanonicalFilePathCache, utilityClang, the name resolvers

#define SRCTRL_MODULE_PURVIEW
// The whole textual purview is a linkage-specification: every declaration AND definition the
// headers/.inls bring in attaches to the GLOBAL module ([module.unit]/7), keeping one entity and
// one ordinary mangling across importer TUs, classic TUs, and moc-generated TUs (SRCTRL_EXPORT's
// `export extern "C++"` handles declarations; this block covers the .inl definitions too).
extern "C++" {
// LOG_* macro definitions only (in the purview the header strips its backend includes); the
// expansions name the LogManager imported from srctrl.logging.
#include "logging.h"
// Class definitions in dependency order (mid-level indexing layer, recorders, components, then the
// visitor whose tuple holds the components by value), then all inline bodies (each header's own
// bottom-include is purview-guarded, so every class is complete before any body parses).
#include "CxxLocationExtractor.h"
#include "CxxSymbolRegistry.h"
#include "CxxConceptReferenceRecorder.h"
#include "CxxDestructorCallRecorder.h"
#include "CxxIndexingContext.h"
#include "CxxAstVisitorComponent.h"
#include "CxxAstVisitorComponentContext.h"
#include "CxxAstVisitorComponentTypeRefKind.h"
#include "CxxAstVisitorComponentDeclRefKind.h"
#include "CxxAstVisitorComponentImplicitCode.h"
#include "CxxAstVisitorComponentDeclarationIndexer.h"
#include "CxxAstVisitorComponentTypeIndexer.h"
#include "CxxAstVisitorComponentReferenceIndexer.h"
#include "CxxAstVisitorComponentModuleIndexer.h"
#include "CxxAstVisitorComponentBraceRecorder.h"
#include "CxxAstVisitor.h"

#include "CxxAstVisitorComponent.inl"
#include "CxxLocationExtractor.inl"
#include "CxxSymbolRegistry.inl"
#include "CxxConceptReferenceRecorder.inl"
#include "CxxDestructorCallRecorder.inl"
#include "CxxIndexingContext.inl"
#include "CxxAstVisitorComponentContext.inl"
#include "CxxAstVisitorComponentTypeRefKind.inl"
#include "CxxAstVisitorComponentDeclRefKind.inl"
#include "CxxAstVisitorComponentImplicitCode.inl"
#include "CxxAstVisitorComponentDeclarationIndexer.inl"
#include "CxxAstVisitorComponentTypeIndexer.inl"
#include "CxxAstVisitorComponentReferenceIndexer.inl"
#include "CxxAstVisitorComponentModuleIndexer.inl"
#include "CxxAstVisitorComponentBraceRecorder.inl"
#include "CxxAstVisitor.inl"

// close the purview-wide extern "C++" linkage block
}
