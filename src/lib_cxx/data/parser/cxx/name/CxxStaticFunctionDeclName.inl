// Inline implementations for CxxStaticFunctionDeclName.h. Included at the end of that header; not a
// standalone TU.

#pragma once

inline CxxStaticFunctionDeclName::CxxStaticFunctionDeclName(
	std::string name,
	std::vector<std::string> templateParameterNames,
	std::unique_ptr<CxxTypeName> returnTypeName,
	std::vector<std::unique_ptr<CxxTypeName>> parameterTypeNames,
	std::string declaringFileName)
	: CxxFunctionDeclName(
		  std::move(name),
		  std::move(templateParameterNames),
		  std::move(returnTypeName),
		  std::move(parameterTypeNames),
		  CxxQualifierFlags::QualifierType::NONE,
		  true)
	, m_declaringFileName(std::move(declaringFileName))
{
}

inline NameHierarchy CxxStaticFunctionDeclName::toNameHierarchy() const
{
	NameHierarchy ret = CxxFunctionDeclName::toNameHierarchy();
	NameElement& last = ret.back();
	last.setSignature(
		last.getSignature().getPrefix(),
		last.getSignature().getPostfix() + " (" + m_declaringFileName + ')');
	return ret;
}
