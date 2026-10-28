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

	// A dependent specialization in a template's signature, `std::vector<T>`, is named with its
	// arguments, so that `f(std::vector<T>)` and `f(std::vector<T*>)` are two names. Without this
	// it is named as the template it specializes, `std::vector<class _Tp, class _Alloc>`: the node
	// a type use of it links to. Resolvers made from this one inherit it.
	void spellDependentSpecializations();
	bool spellsDependentSpecializations() const;

protected:
	CanonicalFilePathCache* getCanonicalFilePathCache() const;
	const std::vector<const clang::Decl*>& getIgnoredContextDecls() const;

private:
	CanonicalFilePathCache* m_canonicalFilePathCache;
	std::vector<const clang::Decl*> m_ignoredContextDecls;
	bool m_spellsDependentSpecializations = false;
};

// Classic build: converge on the family apex, whose bottom includes all resolver bodies once
// every class definition is complete (see CxxNameResolverBodies.h).
#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxDeclNameResolver.h"
#endif

#endif	  // CXX_NAME_RESOLVER_H
