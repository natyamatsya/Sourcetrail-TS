// Inline implementations for CxxQualifierFlags.h. Included at the end of that header; not a
// standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include "utilityEnum.h"
#endif

inline CxxQualifierFlags::CxxQualifierFlags(QualifierType qualifier)
{
	addQualifier(qualifier);
}

inline void CxxQualifierFlags::addQualifier(QualifierType qualifier)
{
	m_flags = m_flags | qualifier;
}

inline bool CxxQualifierFlags::empty() const
{
	return m_flags == QualifierType::NONE;
}

inline std::string CxxQualifierFlags::toString() const
{
	if ((m_flags & QualifierType::CONSTEXPR) == QualifierType::CONSTEXPR)
		return "constexpr";
	
	if ((m_flags & QualifierType::CONST) == QualifierType::CONST)
		return "const";
	
	return "";
}
