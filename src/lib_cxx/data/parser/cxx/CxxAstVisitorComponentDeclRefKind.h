#ifndef CXX_AST_VISITOR_COMPONENT_DECL_REF_KIND_H
#define CXX_AST_VISITOR_COMPONENT_DECL_REF_KIND_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <vector>

#include "CxxAstVisitorComponent.h"
#include "ReferenceKind.h"
#endif

// This CxxAstVisitorComponent is responsible for recording and providing the context based
// ReferenceKind for each reference to a declaration encountered while traversing the AST. Example:
// void foo() { bar(); } For this snippet the reference to "bar" is used in the context of a call.
SRCTRL_EXPORT class CxxAstVisitorComponentDeclRefKind: public CxxAstVisitorComponent
{
public:
	CxxAstVisitorComponentDeclRefKind(CxxAstVisitor* astVisitor);

	ReferenceKind getReferenceKind() const;

	void beginTraverseDecl(clang::Decl* d);
	void endTraverseDecl(clang::Decl* d);

	void beginTraverseStmt(clang::Stmt* s);
	void endTraverseStmt(clang::Stmt* s);

	void beginTraverseType(const clang::QualType& t);
	void endTraverseType(const clang::QualType& t);

	void beginTraverseTypeLoc(const clang::TypeLoc& tl);
	void endTraverseTypeLoc(const clang::TypeLoc& tl);

	void beginTraverseCallCommonCallee();

	void beginTraverseCallCommonArgument();

	void beginTraverseBinCommaLhs();

	void beginTraverseBinCommaRhs();

	void beginTraverseAssignCommonLhs();

	void beginTraverseAssignCommonRhs();

	void beginTraverseConstructorInitializer(clang::CXXCtorInitializer* init);

	void beginTraverseCXXTemporaryObjectExpr(clang::CXXTemporaryObjectExpr* s);

	void beginTraverseUnresolvedMemberExpr(clang::UnresolvedMemberExpr* s);

	void visitVarDecl(clang::VarDecl* d);

	void visitCastExpr(clang::CastExpr* s);

	void visitUnaryAddrOf(clang::UnaryOperator* s);

	void visitUnaryDeref(clang::UnaryOperator* s);

	void visitDeclStmt(clang::DeclStmt* s);

	void visitReturnStmt(clang::ReturnStmt* s);

	void visitInitListExpr(clang::InitListExpr* s);

	void visitMemberExpr(clang::MemberExpr* s);

	void visitCXXDependentScopeMemberExpr(clang::CXXDependentScopeMemberExpr* s);

private:
	void saveAll();
	void restoreAll();

	ReferenceKind m_thisRefKind = ReferenceKind::USAGE;
	ReferenceKind m_childRefKind = ReferenceKind::USAGE;

	std::vector<ReferenceKind> m_oldThisRefKinds;
	std::vector<ReferenceKind> m_oldChildRefKinds;
};


// Classic build: converge on the family apex, whose bottom includes all visitor-blob bodies once
// every class definition is complete (see CxxAstVisitorBodies.h).
#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxAstVisitor.h"
#endif

#endif	  // CXX_AST_VISITOR_COMPONENT_DECL_REF_KIND_H
