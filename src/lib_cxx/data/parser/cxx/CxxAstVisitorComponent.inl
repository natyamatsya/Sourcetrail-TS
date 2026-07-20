// Inline implementations for CxxAstVisitorComponent.h. Included via CxxAstVisitorBodies.h (classic) or the
// srctrl.cxx:visitor wrapper (purview); not a standalone TU.

#pragma once

inline CxxAstVisitorComponent::CxxAstVisitorComponent(CxxAstVisitor* astVisitor): m_astVisitor(astVisitor)
{
}

inline CxxAstVisitor* CxxAstVisitorComponent::getAstVisitor()
{
	return m_astVisitor;
}

inline const CxxAstVisitor* CxxAstVisitorComponent::getAstVisitor() const
{
	return m_astVisitor;
}
