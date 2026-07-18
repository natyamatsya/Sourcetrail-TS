#ifndef CXX_AST_VISITOR_COMPONENT_INDEXER_H
#define CXX_AST_VISITOR_COMPONENT_INDEXER_H

#include <map>

#include "CxxAstVisitorComponent.h"
#include "CxxContext.h"
#include "CxxLocationExtractor.h"
#include "CxxSymbolRegistry.h"
#include "ParseLocation.h"
#include "ReferenceKind.h"
#include "SymbolKind.h"

class ParserClient;
class NameHierarchy;

// This CxxAstVisitorComponent is responsible for recording all symbols and relations throughout the
// visited AST.
class CxxAstVisitorComponentIndexer: public CxxAstVisitorComponent
{
public:
	CxxAstVisitorComponentIndexer(
		CxxAstVisitor* astVisitor, clang::ASTContext* astContext, ParserClient& client);

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
	void recordTemplateMemberSpecialization(
		const clang::MemberSpecializationInfo* memberSpecializationInfo,
		Id contextId,
		const ParseLocation& location,
		SymbolKind symbolKind);

	void recordTemplateParameterConceptReferences(const clang::TemplateDecl *templateDecl);
	template <typename T> void recordConceptReference(const T *d);
	void recordNamedConceptReference(const clang::ConceptReference *conceptReference);

	void recordDeducedType(const clang::DeducedType *autoType, const Id contextSymbolId, const ParseLocation &keywordLocation);
	void recordDeducedQualType(const clang::QualType deducedQualType, const Id contextSymbolId, const ParseLocation &keywordLocation);

	void recordNonTrivialDestructorCalls(const clang::FunctionDecl *d);

	// Axis-2/3 metadata off a decl's attributes: [[deprecated]] -> the modifier
	// bit + an optional DEPRECATED message row (context/DESIGN_NODE_MODIFIERS.md).
	void recordDeprecation(Id symbolId, const clang::Decl* d);

	std::string getLocalSymbolName(const clang::SourceLocation& loc) const;

	ReferenceKind consumeDeclRefContextKind();

	clang::ASTContext* m_astContext;
	ParserClient& m_client;

	CxxSymbolRegistry m_symbols;
	CxxLocationExtractor& m_locations;
};

#endif	  // CXX_AST_VISITOR_COMPONENT_INDEXER_H
