#ifndef CXX_SPECIFIER_NAME_RESOLVER_H
#define CXX_SPECIFIER_NAME_RESOLVER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxName.h"
#include "CxxNameResolver.h"
#include "clang_compat/ClangCompat.h"
#endif

SRCTRL_EXPORT class CxxSpecifierNameResolver: public CxxNameResolver
{
public:
	CxxSpecifierNameResolver(CanonicalFilePathCache* canonicalFilePathCache);
	CxxSpecifierNameResolver(const CxxNameResolver* other);

	CxxName getName(clang_compat::NestedNameSpecifierRef nestedNameSpecifier);
};

// Classic build: converge on the family apex, whose bottom includes all resolver bodies once
// every class definition is complete (see CxxNameResolverBodies.h).
#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxDeclNameResolver.h"
#endif

#endif	  // CXX_SPECIFIER_NAME_RESOLVER_H
