// Inline implementations for CxxTypeName.h. Included at the end of that header; not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <sstream>
#endif

inline std::unique_ptr<CxxTypeName> CxxTypeName::getUnsolved()
{
	return std::make_unique<CxxTypeName>("unsolved-type");
}

inline std::unique_ptr<CxxTypeName> CxxTypeName::makeUnsolvedIfNull(std::unique_ptr<CxxTypeName> name)
{
	if (name)
	{
		return name;
	}

	return getUnsolved();
}

inline CxxTypeName::Modifier::Modifier(std::string symbol): symbol(std::move(symbol)) {}

inline CxxTypeName::CxxTypeName(std::string name): m_name(std::move(name)) {}

inline CxxTypeName::CxxTypeName(std::string name, std::vector<std::string> templateArguments)
	: m_name(std::move(name)), m_templateArguments(std::move(templateArguments))
{
}

inline CxxTypeName::CxxTypeName(
	std::string name, std::vector<std::string> templateArguments, CxxName parent)
	: m_name(std::move(name)), m_templateArguments(std::move(templateArguments))
{
	setParent(std::move(parent));
}

inline NameHierarchy CxxTypeName::toNameHierarchy() const
{
	NameHierarchy ret = getParent() ? getParent().toNameHierarchy()
									: NameHierarchy(NameDelimiterType::CXX);
	ret.push(m_name + getTemplateSuffix(m_templateArguments));
	return ret;
}

inline void CxxTypeName::addQualifier(const CxxQualifierFlags::QualifierType qualifier)
{
	if (m_modifiers.empty())
	{
		m_qualifierFlags.addQualifier(qualifier);
	}
	else
	{
		m_modifiers.back().qualifierFlags.addQualifier(qualifier);
	}
}

inline void CxxTypeName::addModifier(Modifier modifier)
{
	m_modifiers.emplace_back(std::move(modifier));
}

inline std::string CxxTypeName::toString() const
{
	std::stringstream ss;
	if (!m_qualifierFlags.empty())
	{
		ss << m_qualifierFlags.toString() << ' ';
	}

	ss << toNameHierarchy().getQualifiedName();

	for (const Modifier& modifier: m_modifiers)
	{
		ss << ' ' << modifier.symbol;
		if (!modifier.qualifierFlags.empty())
		{
			ss << ' ' << modifier.qualifierFlags.toString();
		}
	}
	return ss.str();
}
