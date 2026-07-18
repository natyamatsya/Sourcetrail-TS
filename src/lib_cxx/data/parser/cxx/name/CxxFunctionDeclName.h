#ifndef CXX_FUNCTION_DECL_NAME_H
#define CXX_FUNCTION_DECL_NAME_H

#include <memory>
#include <vector>

#include "CxxDeclName.h"
#include "CxxTypeName.h"
#include "CxxQualifierFlags.h"

class CxxFunctionDeclName: public CxxDeclName
{
public:
	CxxFunctionDeclName(
		std::string name,
		std::vector<std::string> templateParameterNames,
		std::unique_ptr<CxxTypeName> returnTypeName,
		std::vector<std::unique_ptr<CxxTypeName>> parameterTypeNames,
		CxxQualifierFlags qualifierFlags,
		const bool isStatic);

	NameHierarchy toNameHierarchy() const;

private:
	const std::unique_ptr<CxxTypeName> m_returnTypeName;
	const std::vector<std::unique_ptr<CxxTypeName>> m_parameterTypeNames;
	const CxxQualifierFlags m_qualifierFlags;
	const bool m_isStatic;
};

#endif	  // CXX_FUNCTION_DECL_NAME_H
