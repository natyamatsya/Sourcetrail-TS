#ifndef CXX_NAME_RESOLVER_H
#define CXX_NAME_RESOLVER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <vector>

#include <clang/AST/Decl.h>
#endif

class CanonicalFilePathCache;

SRCTRL_EXPORT class CxxNameResolver
{
public:
	CxxNameResolver(CanonicalFilePathCache* canonicalFilePathCache);
	CxxNameResolver(const CxxNameResolver* other);

	void ignoreContextDecl(const clang::Decl* decl);
	bool ignoresContext(const clang::Decl* decl) const;
	bool ignoresContext(const clang::DeclContext* declContext) const;

protected:
	CanonicalFilePathCache* getCanonicalFilePathCache() const;
	const std::vector<const clang::Decl*>& getIgnoredContextDecls() const;

private:
	CanonicalFilePathCache* m_canonicalFilePathCache;
	std::vector<const clang::Decl*> m_ignoredContextDecls;
};

// Classic build: converge on the family apex, whose bottom includes all resolver bodies once
// every class definition is complete (see CxxNameResolverBodies.h).
#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxDeclNameResolver.h"
#endif

#endif	  // CXX_NAME_RESOLVER_H
