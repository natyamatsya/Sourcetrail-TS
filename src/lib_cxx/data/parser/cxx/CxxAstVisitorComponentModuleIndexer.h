#ifndef CXX_AST_VISITOR_COMPONENT_MODULE_INDEXER_H
#define CXX_AST_VISITOR_COMPONENT_MODULE_INDEXER_H

#include "CxxAstVisitorComponent.h"

class CxxIndexingContext;

// Records C++20 named-module structure: the module a translation unit defines
// (`export module foo;` / `module foo:part;`) as a MODULE node, `import`
// declarations as IMPORT edges between modules, and the NODE_MODIFIER_EXPORTED
// flag on declarations under an `export` region. Split out of the indexer as its
// own node category, alongside the declaration/reference/type indexers.
class CxxAstVisitorComponentModuleIndexer: public CxxAstVisitorComponent
{
public:
	CxxAstVisitorComponentModuleIndexer(
		CxxAstVisitor* astVisitor, clang::ASTContext* astContext, CxxIndexingContext& index);

	// `export module foo;` — record the module this TU defines (fires once per TU;
	// a no-op for non-module translation units).
	void visitTranslationUnitDecl(clang::TranslationUnitDecl* d);
	// `import foo;` / `import :part;` / `import <header>;` — an IMPORT edge from the
	// current module (or the TU) to the imported module.
	void visitImportDecl(clang::ImportDecl* d);
	// `export ...` — flag the declarations in the export region as exported.
	void visitExportDecl(clang::ExportDecl* d);

private:
	clang::ASTContext* m_astContext;

	// The mid-level indexing API: symbol identity, locations, the storage client, and the current
	// context, plus the recordDeclaration / recordReference idioms. Owned by CxxAstVisitor.
	CxxIndexingContext& m_index;
};

#endif	  // CXX_AST_VISITOR_COMPONENT_MODULE_INDEXER_H
