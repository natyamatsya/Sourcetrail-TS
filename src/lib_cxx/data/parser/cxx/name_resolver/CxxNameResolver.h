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

	// The types named are those of `declaration`'s own name: a function's signature, a variable's
	// or field's type, a non-type template parameter's type. Resolvers made from this one inherit it.
	// - A dependent specialization, `std::vector<T>`, is named with its arguments, so that
	//   `f(std::vector<T>)` and `f(std::vector<T*>)` are two names. Outside a declaration's name it
	//   is named as the template it specializes, `std::vector<class _Tp, class _Alloc>`: the node a
	//   type use of it links to.
	// - A member typedef of a class template that does not depend on the template's parameters
	//   (`typedef size_t size_type;`) is the template's own typedef wherever it is used. It is
	//   named as the instantiation's when `declaration` is a member of one, `std::array<bool, 3>::
	//   size_type`, and outside the template as the type it names: not as the primary's
	//   `std::array<class _Tp, size_t _Size>::size_type`.
	void nameTypesOf(const clang::Decl* declaration);
	const clang::Decl* getNamedDeclaration() const;
	bool spellsDependentSpecializations() const;

protected:
	CanonicalFilePathCache* getCanonicalFilePathCache() const;
	const std::vector<const clang::Decl*>& getIgnoredContextDecls() const;

private:
	CanonicalFilePathCache* m_canonicalFilePathCache;
	std::vector<const clang::Decl*> m_ignoredContextDecls;
	const clang::Decl* m_namedDeclaration = nullptr;
};

// Classic build: converge on the family apex, whose bottom includes all resolver bodies once
// every class definition is complete (see CxxNameResolverBodies.h).
#ifndef SRCTRL_MODULE_PURVIEW
#include "CxxDeclNameResolver.h"
#endif

#endif	  // CXX_NAME_RESOLVER_H
