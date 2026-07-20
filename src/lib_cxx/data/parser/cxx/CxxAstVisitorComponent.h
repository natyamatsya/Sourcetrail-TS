#ifndef CXX_AST_VISITOR_COMPONENT_H
#define CXX_AST_VISITOR_COMPONENT_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <clang/AST/RecursiveASTVisitor.h>
#endif

SRCTRL_EXPORT class CxxAstVisitor;

// CxxAstVisitorComponent: This is the base class for all ast visitor components.
// Each component can override it's begin-/endTraverse and visit methods in order to provide some
// functionality. The CxxAstVisitor executes all of these methods of registered components while
// traversing the AST.
SRCTRL_EXPORT class CxxAstVisitorComponent
{
public:
	CxxAstVisitorComponent(CxxAstVisitor* astVisitor);

	// Called once by CxxAstVisitor after all components are constructed. A component overrides it
	// to cache pointers to the sibling components it depends on (which cannot be fetched during
	// construction, as the component tuple is not yet fully built).
	void wire() {}

#define DEF_TRAVERSE_CUSTOM_TYPE_PTR(__NAME_TYPE__, __PARAM_TYPE__)                                \
	void beginTraverse##__NAME_TYPE__(clang::__PARAM_TYPE__* /*v*/) {}                                 \
	void endTraverse##__NAME_TYPE__(clang::__PARAM_TYPE__* /*v*/) {}

#define DEF_TRAVERSE_CUSTOM_TYPE(__NAME_TYPE__, __PARAM_TYPE__)                                    \
	void beginTraverse##__NAME_TYPE__(const clang::__PARAM_TYPE__& /*v*/) {}                           \
	void endTraverse##__NAME_TYPE__(const clang::__PARAM_TYPE__& /*v*/) {}

#define DEF_TRAVERSE_TYPE_PTR(__TYPE__) DEF_TRAVERSE_CUSTOM_TYPE_PTR(__TYPE__, __TYPE__)

#define DEF_TRAVERSE_TYPE(__TYPE__) DEF_TRAVERSE_CUSTOM_TYPE(__TYPE__, __TYPE__)

	DEF_TRAVERSE_TYPE_PTR(Decl)

	DEF_TRAVERSE_TYPE_PTR(Stmt)

	DEF_TRAVERSE_CUSTOM_TYPE(Type, QualType)

	DEF_TRAVERSE_TYPE(TypeLoc)

	DEF_TRAVERSE_TYPE_PTR(FunctionDecl)

	DEF_TRAVERSE_TYPE_PTR(ClassTemplateSpecializationDecl)

	DEF_TRAVERSE_TYPE_PTR(ClassTemplatePartialSpecializationDecl)

	DEF_TRAVERSE_TYPE(TemplateSpecializationTypeLoc)

	DEF_TRAVERSE_TYPE_PTR(LambdaExpr)

	DEF_TRAVERSE_TYPE_PTR(DeclRefExpr)

	DEF_TRAVERSE_TYPE_PTR(CXXForRangeStmt)

	DEF_TRAVERSE_TYPE_PTR(UnresolvedLookupExpr)

	DEF_TRAVERSE_TYPE_PTR(UnresolvedMemberExpr)

	void beginTraverseCallCommonCallee() {}
	void endTraverseCallCommonCallee() {}

	void beginTraverseCallCommonArgument() {}
	void endTraverseCallCommonArgument() {}

	void beginTraverseBinCommaLhs() {}
	void endTraverseBinCommaLhs() {}

	void beginTraverseBinCommaRhs() {}
	void endTraverseBinCommaRhs() {}

	void beginTraverseAssignCommonLhs() {}
	void endTraverseAssignCommonLhs() {}

	void beginTraverseAssignCommonRhs() {}
	void endTraverseAssignCommonRhs() {}

	void beginTraverseCXXBaseSpecifier() {}
	void endTraverseCXXBaseSpecifier() {}

	void beginTraverseTemplateDefaultArgumentLoc() {}
	void endTraverseTemplateDefaultArgumentLoc() {}

	DEF_TRAVERSE_TYPE(NestedNameSpecifierLoc)

	DEF_TRAVERSE_CUSTOM_TYPE_PTR(ConstructorInitializer, CXXCtorInitializer)

	DEF_TRAVERSE_TYPE_PTR(CXXTemporaryObjectExpr)

	void beginTraverseTemplateArgumentLoc(const clang::TemplateArgumentLoc&  /*loc*/) {}
	void endTraverseTemplateArgumentLoc(const clang::TemplateArgumentLoc&  /*loc*/) {}

	void beginTraverseLambdaCapture(clang::LambdaExpr*  /*lambdaExpr*/, const clang::LambdaCapture*  /*capture*/)
	{
	}
	void endTraverseLambdaCapture(clang::LambdaExpr*  /*lambdaExpr*/, const clang::LambdaCapture*  /*capture*/)
	{
	}

	void visitTranslationUnitDecl(clang::TranslationUnitDecl*  /*d*/) {}
	void visitImportDecl(clang::ImportDecl*  /*d*/) {}
	void visitExportDecl(clang::ExportDecl*  /*d*/) {}
	void visitTagDecl(clang::TagDecl*  /*d*/) {}
	void visitClassTemplateDecl(clang::ClassTemplateDecl * /*d*/) {}
	void visitClassTemplateSpecializationDecl(clang::ClassTemplateSpecializationDecl*  /*d*/) {}
	void visitVarDecl(clang::VarDecl*  /*d*/) {}
	void visitDecompositionDecl(clang::DecompositionDecl *) {}
	void visitVarTemplateSpecializationDecl(clang::VarTemplateSpecializationDecl*  /*d*/) {}
	void visitFieldDecl(clang::FieldDecl*  /*d*/) {}
	void visitFunctionDecl(clang::FunctionDecl*  /*d*/) {}
	void visitFunctionTemplateDecl(clang::FunctionTemplateDecl * /*d*/) {}
	void visitCXXMethodDecl(clang::CXXMethodDecl*  /*d*/) {}
	void visitEnumConstantDecl(clang::EnumConstantDecl*  /*d*/) {}
	void visitNamespaceDecl(clang::NamespaceDecl*  /*d*/) {}
	void visitNamespaceAliasDecl(clang::NamespaceAliasDecl*  /*d*/) {}
	void visitTypedefDecl(clang::TypedefDecl*  /*d*/) {}
	void visitTypeAliasDecl(clang::TypeAliasDecl*  /*d*/) {}
	void visitUsingDirectiveDecl(clang::UsingDirectiveDecl*  /*d*/) {}
	void visitUsingDecl(clang::UsingDecl*  /*d*/) {}
	void visitNonTypeTemplateParmDecl(clang::NonTypeTemplateParmDecl*  /*d*/) {}
	void visitTemplateTypeParmDecl(clang::TemplateTypeParmDecl*  /*d*/) {}
	void visitTemplateTemplateParmDecl(clang::TemplateTemplateParmDecl*  /*d*/) {}
	void visitConceptDecl(clang::ConceptDecl * /*d*/) {}
	void visitConceptSpecializationExpr(clang::ConceptSpecializationExpr * /*d*/) {}
	void visitConceptReference(clang::ConceptReference *) {}

	void visitTypeLoc(clang::TypeLoc  /*tl*/) {}

	void visitCastExpr(clang::CastExpr*  /*s*/) {}
	void visitCXXFunctionalCastExpr(clang::CXXFunctionalCastExpr *) {}
	void visitUnaryAddrOf(clang::UnaryOperator*  /*s*/) {}
	void visitUnaryDeref(clang::UnaryOperator*  /*s*/) {}
	void visitDeclStmt(clang::DeclStmt*  /*s*/) {}
	void visitReturnStmt(clang::ReturnStmt*  /*s*/) {}
	void visitCompoundStmt(clang::CompoundStmt*  /*s*/) {}
	void visitInitListExpr(clang::InitListExpr*  /*s*/) {}
	void visitDeclRefExpr(clang::DeclRefExpr*  /*s*/) {}
	void visitMemberExpr(clang::MemberExpr*  /*s*/) {}
	void visitCXXDependentScopeMemberExpr(clang::CXXDependentScopeMemberExpr*  /*s*/) {}
	void visitCXXConstructExpr(clang::CXXConstructExpr*  /*s*/) {}
	void visitCXXDeleteExpr(clang::CXXDeleteExpr*  /*s*/) {}
	void visitLambdaExpr(clang::LambdaExpr*  /*s*/) {}
	void visitMSAsmStmt(clang::MSAsmStmt*  /*s*/) {}

	void visitConstructorInitializer(clang::CXXCtorInitializer*  /*init*/) {}

#undef DEF_TRAVERSE_CUSTOM_TYPE_PTR
#undef DEF_TRAVERSE_CUSTOM_TYPE
#undef DEF_TRAVERSE_TYPE_PTR
#undef DEF_TRAVERSE_TYPE

protected:
	CxxAstVisitor* getAstVisitor();
	const CxxAstVisitor* getAstVisitor() const;

private:
	CxxAstVisitor* m_astVisitor;
};


// NOTE: no classic bottom-include here -- this header is top-included by other blob headers, so a
// bottom-include of the apex could fire while an includer's class is still incomplete. Classic
// consumers reach the inline bodies through any converging blob header (see CxxAstVisitorBodies.h).

#endif	  // CXX_AST_VISITOR_COMPONENT_H
