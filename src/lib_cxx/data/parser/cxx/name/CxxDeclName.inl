// Inline implementations for CxxDeclName.h. Included at the end of that header; not a standalone TU.

#pragma once

inline CxxDeclName::CxxDeclName(std::string name)
	: m_name(std::move(name))
{
}

inline CxxDeclName::CxxDeclName(std::string name, std::vector<std::string> templateParameterNames)
	: m_name(std::move(name))
	, m_templateParameterNames(std::move(templateParameterNames))
{
}

inline NameHierarchy CxxDeclName::toNameHierarchy() const
{
	NameHierarchy ret = getParent() ? getParent().toNameHierarchy()
									: NameHierarchy(NameDelimiterType::CXX);
	ret.push(m_name + getTemplateSuffix(m_templateParameterNames));
	return ret;
}

inline const std::string& CxxDeclName::getName() const
{
	return m_name;
}

inline const std::vector<std::string>& CxxDeclName::getTemplateParameterNames() const
{
	return m_templateParameterNames;
}
