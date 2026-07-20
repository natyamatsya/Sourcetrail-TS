#ifndef CXX_STATIC_FUNCTION_DECL_NAME_H
#define CXX_STATIC_FUNCTION_DECL_NAME_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxFunctionDeclName.h"
#endif

SRCTRL_EXPORT class CxxStaticFunctionDeclName: public CxxFunctionDeclName
{
public:
	CxxStaticFunctionDeclName(
		std::string name,
		std::vector<std::string> templateParameterNames,
		std::unique_ptr<CxxTypeName> returnTypeName,
		std::vector<std::unique_ptr<CxxTypeName>> parameterTypeNames,
		std::string translationUnitFileName);

	NameHierarchy toNameHierarchy() const;

private:
	std::string m_translationUnitFileName;
};

#include "CxxStaticFunctionDeclName.inl"

#endif	  // CXX_FUNCTION_DECL_NAME_H
