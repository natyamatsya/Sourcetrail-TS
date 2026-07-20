#ifndef CXX_AST_VISITOR_COMPONENT_IMPLICIT_CODE_H
#define CXX_AST_VISITOR_COMPONENT_IMPLICIT_CODE_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxAstVisitorComponent.h"
#endif

// This CxxAstVisitorComponent is responsible for deciding if the AstVisitor should visit implicit
// code in the current context.
SRCTRL_EXPORT class CxxAstVisitorComponentImplicitCode: public CxxAstVisitorComponent
{
public:
	CxxAstVisitorComponentImplicitCode(CxxAstVisitor* astVisitor);

	bool shouldVisitImplicitCode() const;

	void beginTraverseDecl(clang::Decl* d);
	void endTraverseDecl(clang::Decl* d);

	void beginTraverseCXXForRangeStmt(clang::CXXForRangeStmt* s);
	void endTraverseCXXForRangeStmt(clang::CXXForRangeStmt* s);

private:
	std::vector<bool> m_stack;
};


// Classic build: converge on the family apex, whose bottom includes all visitor-blob bodies once
// every class definition is complete (see CxxAstVisitorBodies.h).
#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxAstVisitor.h"
#endif

#endif	  // CXX_AST_VISITOR_COMPONENT_IMPLICIT_CODE_H
