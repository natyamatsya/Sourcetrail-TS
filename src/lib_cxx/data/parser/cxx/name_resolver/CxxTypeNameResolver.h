#ifndef CXX_TYPE_NAME_RESOLVER_H
#define CXX_TYPE_NAME_RESOLVER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxNameResolver.h"
#include "CxxTypeName.h"
#endif

SRCTRL_EXPORT class CxxTypeNameResolver: public CxxNameResolver
{
public:
	CxxTypeNameResolver(CanonicalFilePathCache* canonicalFilePathCache);
	CxxTypeNameResolver(const CxxNameResolver* other);

	std::unique_ptr<CxxTypeName> getName(const clang::QualType& qualType, const clang::VarDecl *varDecl = nullptr);
	std::unique_ptr<CxxTypeName> getName(const clang::Type* type);
};

// Classic build: converge on the family apex, whose bottom includes all resolver bodies once
// every class definition is complete (see CxxNameResolverBodies.h).
#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxDeclNameResolver.h"
#endif

#endif	  // CXX_TYPE_NAME_RESOLVER_H
