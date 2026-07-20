#ifndef CXX_TEMPLATE_ARGUMENT_NAME_RESOLVER_H
#define CXX_TEMPLATE_ARGUMENT_NAME_RESOLVER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <memory>

#include "CxxNameResolver.h"
#endif

class DataType;

SRCTRL_EXPORT class CxxTemplateArgumentNameResolver: public CxxNameResolver
{
public:
	CxxTemplateArgumentNameResolver(const CxxNameResolver* other);

	std::string getTemplateArgumentName(const clang::TemplateArgument& argument);
};

// Classic build: converge on the family apex, whose bottom includes all resolver bodies once
// every class definition is complete (see CxxNameResolverBodies.h).
#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxDeclNameResolver.h"
#endif

#endif	  // CXX_TEMPLATE_ARGUMENT_NAME_RESOLVER_H
