#ifndef CXX_AST_VISITOR_COMPONENT_MODULE_INDEXER_H
#define CXX_AST_VISITOR_COMPONENT_MODULE_INDEXER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxAstVisitorComponent.h"
#endif

SRCTRL_EXPORT class CxxIndexingContext;

// Records C++20 named-module structure: the module a translation unit defines
// (`export module foo;` / `module foo:part;`) as a MODULE node, `import`
// declarations as IMPORT edges between modules, and the NODE_MODIFIER_EXPORTED
// flag on declarations under an `export` region. Split out of the indexer as its
// own node category, alongside the declaration/reference/type indexers.
SRCTRL_EXPORT class CxxAstVisitorComponentModuleIndexer: public CxxAstVisitorComponent
{
public:
	CxxAstVisitorComponentModuleIndexer(
		CxxAstVisitor* astVisitor, clang::ASTContext* astContext, CxxIndexingContext& index);

	// `export module foo;` / `module foo:part;` — record the module (partition) this TU builds
	// (fires once per TU; a no-op for implementation units and non-module translation units).
	void visitTranslationUnitDecl(clang::TranslationUnitDecl* d);
	// `import foo;` / `import :part;` / `import <header>;` — an IMPORT edge from the
	// current module (or, outside an interface, the importing file) to the imported module.
	void visitImportDecl(clang::ImportDecl* d);
	// `export ...` — flag the declarations in the export region as exported.
	void visitExportDecl(clang::ExportDecl* d);

private:
	// The module name after a `module` keyword (`a.b:part`), from its first to its last
	// identifier; invalid when there is none to find.
	clang::SourceRange getModuleNameRange(clang::SourceLocation keywordLocation) const;

	clang::ASTContext* m_astContext;

	// The mid-level indexing API: symbol identity, locations, the storage client, and the current
	// context, plus the recordDeclaration / recordReference idioms. Owned by CxxAstVisitor.
	CxxIndexingContext& m_index;
};


// Classic build: converge on the family apex, whose bottom includes all visitor-blob bodies once
// every class definition is complete (see CxxAstVisitorBodies.h).
#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxAstVisitor.h"
#endif

#endif	  // CXX_AST_VISITOR_COMPONENT_MODULE_INDEXER_H
