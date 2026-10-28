// Inline implementations for CxxQualifierFlags.h. Included at the end of that header; not a
// standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <utility>

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

inline bool CxxQualifierFlags::has(QualifierType qualifier) const
{
	return (m_flags & qualifier) == qualifier;
}

// Every qualifier that is set, in the order C++ spells them: `constexpr const volatile &`.
inline std::string CxxQualifierFlags::toString() const
{
	static constexpr std::pair<QualifierType, const char*> spellings[] = {
		{QualifierType::CONSTEXPR, "constexpr"},
		{QualifierType::CONST, "const"},
		{QualifierType::VOLATILE, "volatile"},
		{QualifierType::LVALUE_REFERENCE, "&"},
		{QualifierType::RVALUE_REFERENCE, "&&"},
	};

	std::string result;
	for (const auto& [qualifier, spelling]: spellings)
	{
		if (has(qualifier))
		{
			if (!result.empty())
				result += ' ';
			result += spelling;
		}
	}
	return result;
}
