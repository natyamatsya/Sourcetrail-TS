#ifndef CXX_AST_VISITOR_COMPONENT_TYPE_INDEXER_H
#define CXX_AST_VISITOR_COMPONENT_TYPE_INDEXER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxAstVisitorComponent.h"
#endif

SRCTRL_EXPORT class CxxIndexingContext;
SRCTRL_EXPORT class CxxAstVisitorComponentTypeRefKind;

// Records type usages and the qualifiers/captures encountered while traversing the type system:
// type locations, nested-name-specifier qualifiers, template arguments, and lambda captures. These
// hooks emit TYPE_USAGE / QUALIFIER references and local symbols through the shared
// CxxIndexingContext. Split out of the former monolithic indexer.
SRCTRL_EXPORT class CxxAstVisitorComponentTypeIndexer: public CxxAstVisitorComponent
{
public:
	CxxAstVisitorComponentTypeIndexer(CxxAstVisitor* astVisitor, CxxIndexingContext& index);

	void wire();

	void beginTraverseNestedNameSpecifierLoc(const clang::NestedNameSpecifierLoc& loc);
	void beginTraverseTemplateArgumentLoc(const clang::TemplateArgumentLoc& loc);
	void beginTraverseLambdaCapture(clang::LambdaExpr* lambdaExpr, const clang::LambdaCapture* capture);
	void visitTypeLoc(clang::TypeLoc tl);

private:
	// The mid-level indexing API: symbol identity, locations, the storage client, and the current
	// context, plus the recordDeclaration / recordReference idioms. Owned by CxxAstVisitor.
	CxxIndexingContext& m_index;

	// Sibling queried by visitTypeLoc to distinguish inheritance / template-argument type usages;
	// cached in wire() once the component tuple is fully constructed.
	CxxAstVisitorComponentTypeRefKind* m_typeRefKind = nullptr;
};


// Classic build: converge on the family apex, whose bottom includes all visitor-blob bodies once
// every class definition is complete (see CxxAstVisitorBodies.h).
#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxAstVisitor.h"
#endif

#endif	  // CXX_AST_VISITOR_COMPONENT_TYPE_INDEXER_H
