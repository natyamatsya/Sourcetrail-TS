// Inline implementations for CxxFunctionDeclName.h. Included at the end of that header; not a
// standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <sstream>
#endif

inline CxxFunctionDeclName::CxxFunctionDeclName(
	std::string name,
	std::vector<std::string> templateParameterNames,
	std::unique_ptr<CxxTypeName> returnTypeName,
	std::vector<std::unique_ptr<CxxTypeName>> parameterTypeNames,
	CxxQualifierFlags qualifierFlags,
	const bool isStatic)
	: CxxDeclName(std::move(name), std::move(templateParameterNames))
	, m_returnTypeName(std::move(returnTypeName))
	, m_parameterTypeNames(std::move(parameterTypeNames))
	, m_qualifierFlags(qualifierFlags)
	, m_isStatic(isStatic)
{
}

inline NameHierarchy CxxFunctionDeclName::toNameHierarchy() const
{
	std::stringstream prefix;
	if (m_isStatic)
	{
		prefix << "static ";
	}
	prefix << m_returnTypeName->toString();

	std::stringstream postfix;
	postfix << '(';
	for (size_t i = 0; i < m_parameterTypeNames.size(); i++)
	{
		if (i != 0)
		{
			postfix << ", ";
		}
		postfix << m_parameterTypeNames[i]->toString();
	}
	postfix << ')';
	if (!m_qualifierFlags.empty())
	{
		postfix << " " << m_qualifierFlags.toString();
	}

	NameHierarchy ret = CxxDeclName::toNameHierarchy();
	ret.back().setSignature(prefix.str(), postfix.str());
	return ret;
}
