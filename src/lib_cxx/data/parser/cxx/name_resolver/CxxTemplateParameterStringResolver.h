#ifndef CXX_TEMPLATE_PARAMETER_STRING_RESOLVER_H
#define CXX_TEMPLATE_PARAMETER_STRING_RESOLVER_H


#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <clang/AST/DeclTemplate.h>

#include "CxxNameResolver.h"
#endif

class DataType;

SRCTRL_EXPORT class CxxTemplateParameterStringResolver: public CxxNameResolver
{
public:
	CxxTemplateParameterStringResolver(CanonicalFilePathCache* canonicalFilePathCache);
	CxxTemplateParameterStringResolver(const CxxNameResolver* other);

	std::string getTemplateParameterString(const clang::NamedDecl* parameter);

private:
	std::string getTemplateParameterTypeString(const clang::NonTypeTemplateParmDecl* parameter);
	static std::string getTemplateParameterTypeString(const clang::TemplateTypeParmDecl* parameter);
	std::string getTemplateParameterTypeString(const clang::TemplateTemplateParmDecl* parameter);
};

// Classic build: converge on the family apex, whose bottom includes all resolver bodies once
// every class definition is complete (see CxxNameResolverBodies.h).
#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxDeclNameResolver.h"
#endif

#endif	  // CXX_TEMPLATE_PARAMETER_STRING_RESOLVER_H
