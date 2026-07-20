// Inline implementations for CxxAstVisitorComponentImplicitCode.h. Included via CxxAstVisitorBodies.h (classic) or the
// srctrl.cxx:visitor wrapper (purview); not a standalone TU.

#pragma once

inline CxxAstVisitorComponentImplicitCode::CxxAstVisitorComponentImplicitCode(CxxAstVisitor* astVisitor)
	: CxxAstVisitorComponent(astVisitor)
{
}

inline bool CxxAstVisitorComponentImplicitCode::shouldVisitImplicitCode() const
{
	if (!m_stack.empty())
	{
		return m_stack.back();
	}
	return true;
}

inline void CxxAstVisitorComponentImplicitCode::beginTraverseDecl(clang::Decl*  /*d*/)
{
	m_stack.push_back(true);
}

inline void CxxAstVisitorComponentImplicitCode::endTraverseDecl(clang::Decl*  /*d*/)
{
	m_stack.pop_back();
}

inline void CxxAstVisitorComponentImplicitCode::beginTraverseCXXForRangeStmt(clang::CXXForRangeStmt*  /*s*/)
{
	m_stack.push_back(false);
}

inline void CxxAstVisitorComponentImplicitCode::endTraverseCXXForRangeStmt(clang::CXXForRangeStmt*  /*s*/)
{
	m_stack.pop_back();
}
