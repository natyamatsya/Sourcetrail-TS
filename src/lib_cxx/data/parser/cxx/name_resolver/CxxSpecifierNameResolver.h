#ifndef CXX_SPECIFIER_NAME_RESOLVER_H
#define CXX_SPECIFIER_NAME_RESOLVER_H

#include "CxxName.h"
#include "CxxNameResolver.h"
#include "clang_compat/ClangCompat.h"

class CxxSpecifierNameResolver: public CxxNameResolver
{
public:
	CxxSpecifierNameResolver(CanonicalFilePathCache* canonicalFilePathCache);
	CxxSpecifierNameResolver(const CxxNameResolver* other);

	CxxName getName(clang_compat::NestedNameSpecifierRef nestedNameSpecifier);
};

#endif	  // CXX_SPECIFIER_NAME_RESOLVER_H
