#ifndef CXX_VARIABLE_DECL_NAME_H
#define CXX_VARIABLE_DECL_NAME_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <memory>
#include <vector>

#include "CxxDeclName.h"
#include "CxxTypeName.h"
#endif

SRCTRL_EXPORT class CxxVariableDeclName: public CxxDeclName
{
public:
	CxxVariableDeclName(
		std::string name,
		std::vector<std::string> templateParameterNames,
		std::unique_ptr<CxxTypeName> typeName,
		bool isStatic);

	NameHierarchy toNameHierarchy() const;

private:
	std::unique_ptr<CxxTypeName> m_typeName;
	const bool m_isStatic;
};

#include "CxxVariableDeclName.inl"

#endif	  // CXX_VARIABLE_DECL_NAME_H
