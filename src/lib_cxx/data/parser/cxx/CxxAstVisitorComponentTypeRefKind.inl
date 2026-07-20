// Inline implementations for CxxAstVisitorComponentTypeRefKind.h. Included via CxxAstVisitorBodies.h (classic) or the
// srctrl.cxx:visitor wrapper (purview); not a standalone TU.

#pragma once

inline CxxAstVisitorComponentTypeRefKind::CxxAstVisitorComponentTypeRefKind(CxxAstVisitor* astVisitor)
	: CxxAstVisitorComponent(astVisitor)
{
}

inline bool CxxAstVisitorComponentTypeRefKind::isTraversingInheritance() const
{
	return (!m_stateKindStack.empty() && m_stateKindStack.back() == STATE_INHERITANCE);
}

inline bool CxxAstVisitorComponentTypeRefKind::isTraversingTemplateArgument() const
{
	return (!m_stateKindStack.empty() && m_stateKindStack.back() == STATE_TEMPLATE_ARGUMENT);
}

inline void CxxAstVisitorComponentTypeRefKind::beginTraverseCXXBaseSpecifier()
{
	m_stateKindStack.push_back(STATE_INHERITANCE);
}

inline void CxxAstVisitorComponentTypeRefKind::endTraverseCXXBaseSpecifier()
{
	m_stateKindStack.pop_back();
}

inline void CxxAstVisitorComponentTypeRefKind::beginTraverseTemplateDefaultArgumentLoc()
{
	m_stateKindStack.push_back(STATE_USAGE);
}

inline void CxxAstVisitorComponentTypeRefKind::endTraverseTemplateDefaultArgumentLoc()
{
	m_stateKindStack.pop_back();
}

inline void CxxAstVisitorComponentTypeRefKind::beginTraverseTemplateArgumentLoc(
	const clang::TemplateArgumentLoc&  /*loc*/)
{
	m_stateKindStack.push_back(STATE_TEMPLATE_ARGUMENT);
}

inline void CxxAstVisitorComponentTypeRefKind::endTraverseTemplateArgumentLoc(
	const clang::TemplateArgumentLoc&  /*loc*/)
{
	m_stateKindStack.pop_back();
}
