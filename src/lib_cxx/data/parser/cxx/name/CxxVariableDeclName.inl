// Inline implementations for CxxVariableDeclName.h. Included at the end of that header; not a
// standalone TU.

#pragma once

inline CxxVariableDeclName::CxxVariableDeclName(
	std::string name,
	std::vector<std::string> templateParameterNames,
	std::unique_ptr<CxxTypeName> typeName,
	bool isStatic)
	: CxxDeclName(std::move(name), std::move(templateParameterNames))
	, m_typeName(std::move(typeName))
	, m_isStatic(isStatic)
{
}

inline NameHierarchy CxxVariableDeclName::toNameHierarchy() const
{
	std::string signaturePrefix;
	if (m_isStatic)
	{
		signaturePrefix += "static ";
	}
	signaturePrefix += m_typeName->toString();

	NameHierarchy ret = CxxDeclName::toNameHierarchy();
	ret.back().setSignature(std::move(signaturePrefix), "");
	return ret;
}
