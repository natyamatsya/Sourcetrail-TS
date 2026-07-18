#ifndef CXX_AST_VISITOR_COMPONENT_INDEXER_H
#define CXX_AST_VISITOR_COMPONENT_INDEXER_H

#include "CxxAstVisitorComponent.h"
#include "ReferenceKind.h"

class CxxIndexingContext;
class CxxAstVisitorComponentTypeRefKind;
class CxxAstVisitorComponentDeclRefKind;

// This CxxAstVisitorComponent is responsible for recording all symbols and relations throughout the
// visited AST.
class CxxAstVisitorComponentIndexer: public CxxAstVisitorComponent
{
public:
	CxxAstVisitorComponentIndexer(
		CxxAstVisitor* astVisitor, clang::ASTContext* astContext, CxxIndexingContext& index);

	void wire();

	void beginTraverseNestedNameSpecifierLoc(const clang::NestedNameSpecifierLoc& loc);
	void beginTraverseTemplateArgumentLoc(const clang::TemplateArgumentLoc& loc);
	void beginTraverseLambdaCapture(clang::LambdaExpr* lambdaExpr, const clang::LambdaCapture* capture);

	void visitCastExpr(clang::CastExpr *d);
	void visitCXXFunctionalCastExpr(clang::CXXFunctionalCastExpr *d);
	void visitTagDecl(clang::TagDecl* d);
	void visitClassTemplateDecl(clang::ClassTemplateDecl *d);
	void visitClassTemplateSpecializationDecl(clang::ClassTemplateSpecializationDecl* d);
	void visitVarDecl(clang::VarDecl* d);
	void visitDecompositionDecl(clang::DecompositionDecl *d);
	void visitVarTemplateSpecializationDecl(clang::VarTemplateSpecializationDecl* d);
	void visitFieldDecl(clang::FieldDecl* d);
	void visitFunctionDecl(clang::FunctionDecl* d);
	void visitFunctionTemplateDecl(clang::FunctionTemplateDecl *d);
	void visitCXXMethodDecl(clang::CXXMethodDecl* d);
	void visitEnumConstantDecl(clang::EnumConstantDecl* d);
	void visitNamespaceDecl(clang::NamespaceDecl* d);
	void visitNamespaceAliasDecl(clang::NamespaceAliasDecl* d);
	void visitTypedefDecl(clang::TypedefDecl* d);
	void visitTypeAliasDecl(clang::TypeAliasDecl* d);
	void visitUsingDirectiveDecl(clang::UsingDirectiveDecl* d);
	void visitUsingDecl(clang::UsingDecl* d);
	void visitNonTypeTemplateParmDecl(clang::NonTypeTemplateParmDecl* d);
	void visitTemplateTypeParmDecl(clang::TemplateTypeParmDecl* d);
	void visitTemplateTemplateParmDecl(clang::TemplateTemplateParmDecl* d);
	void visitConceptDecl(clang::ConceptDecl *d);
	void visitConceptSpecializationExpr(clang::ConceptSpecializationExpr *d);
	void visitConceptReference(clang::ConceptReference *d);
	void visitTypeLoc(clang::TypeLoc tl);

	void visitDeclRefExpr(clang::DeclRefExpr* s);
	void visitMemberExpr(clang::MemberExpr* s);
	void visitCXXConstructExpr(clang::CXXConstructExpr* s);
	void visitCXXDeleteExpr(clang::CXXDeleteExpr* s);
	void visitLambdaExpr(clang::LambdaExpr* s);

	void visitConstructorInitializer(clang::CXXCtorInitializer* init);

private:
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

#endif	  // CXX_AST_VISITOR_COMPONENT_INDEXER_H
