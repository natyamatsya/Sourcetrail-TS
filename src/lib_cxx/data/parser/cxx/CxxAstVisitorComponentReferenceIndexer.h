#ifndef CXX_AST_VISITOR_COMPONENT_REFERENCE_INDEXER_H
#define CXX_AST_VISITOR_COMPONENT_REFERENCE_INDEXER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxAstVisitorComponent.h"
#include "ReferenceKind.h"
#endif

SRCTRL_EXPORT class CxxIndexingContext;
SRCTRL_EXPORT class CxxAstVisitorComponentTypeRefKind;
SRCTRL_EXPORT class CxxAstVisitorComponentDeclRefKind;

// Records the reference edges the AST expresses: calls, type usages, constructions, deletions,
// concept-constraint uses, member/decl references and constructor initializers. It classifies each
// decl-reference by consulting the traversal-state components (inheritance / template-argument /
// decl-ref kind) via consumeDeclRefContextKind, then emits the edge through the shared
// CxxIndexingContext. Split out of the former monolithic indexer.
SRCTRL_EXPORT class CxxAstVisitorComponentReferenceIndexer: public CxxAstVisitorComponent
{
public:
	CxxAstVisitorComponentReferenceIndexer(
		CxxAstVisitor* astVisitor, clang::ASTContext* astContext, CxxIndexingContext& index);

	void wire();

	void visitCastExpr(clang::CastExpr* d);
	void visitCXXFunctionalCastExpr(clang::CXXFunctionalCastExpr* d);
	void visitConceptSpecializationExpr(clang::ConceptSpecializationExpr* d);
	void visitConceptReference(clang::ConceptReference* d);
	void visitDeclRefExpr(clang::DeclRefExpr* s);
	void visitMemberExpr(clang::MemberExpr* s);
	void visitCXXConstructExpr(clang::CXXConstructExpr* s);
	void visitCXXDeleteExpr(clang::CXXDeleteExpr* s);
	void visitLambdaExpr(clang::LambdaExpr* s);
	void visitConstructorInitializer(clang::CXXCtorInitializer* init);

private:
	// Classify the reference kind for the decl-reference currently being visited, from the
	// traversal-state siblings (inheritance / template argument / plain decl-ref).
	ReferenceKind consumeDeclRefContextKind();

	clang::ASTContext* m_astContext;

	// The mid-level indexing API: symbol identity, locations, the storage client, and the current
	// context, plus the recordDeclaration / recordReference idioms. Owned by CxxAstVisitor.
	CxxIndexingContext& m_index;

	// Sibling components this one queries during traversal; cached in wire() (see the base class)
	// once the component tuple is fully constructed.
	CxxAstVisitorComponentTypeRefKind* m_typeRefKind = nullptr;
	CxxAstVisitorComponentDeclRefKind* m_declRefKind = nullptr;
};


// Classic build: converge on the family apex, whose bottom includes all visitor-blob bodies once
// every class definition is complete (see CxxAstVisitorBodies.h).
#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxAstVisitor.h"
#endif

#endif	  // CXX_AST_VISITOR_COMPONENT_REFERENCE_INDEXER_H
