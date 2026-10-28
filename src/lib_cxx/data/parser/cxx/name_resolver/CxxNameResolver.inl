// Inline implementations for CxxNameResolver.h. Included via CxxNameResolverBodies.h (classic) or
// the srctrl.cxx:parser wrapper (purview); not a standalone TU.

#pragma once

inline CxxNameResolver::CxxNameResolver(CanonicalFilePathCache* canonicalFilePathCache)
	: m_canonicalFilePathCache(canonicalFilePathCache)
{
}

inline CxxNameResolver::CxxNameResolver(const CxxNameResolver* other)
	: m_canonicalFilePathCache(other->getCanonicalFilePathCache())
	, m_ignoredContextDecls(other->getIgnoredContextDecls())
	, m_namedDeclaration(other->getNamedDeclaration())
{
}

inline void CxxNameResolver::nameTypesOf(const clang::Decl* declaration)
{
	m_namedDeclaration = declaration;
}

inline const clang::Decl* CxxNameResolver::getNamedDeclaration() const
{
	return m_namedDeclaration;
}

inline bool CxxNameResolver::spellsDependentSpecializations() const
{
	return m_namedDeclaration != nullptr;
}

inline void CxxNameResolver::ignoreContextDecl(const clang::Decl* decl)
{
	if (decl)
	{
		m_ignoredContextDecls.emplace_back(decl);
	}
}

inline bool CxxNameResolver::ignoresContext(const clang::Decl* decl) const
{
	if (decl)
	{
		for (const clang::Decl* ignoredDecl: m_ignoredContextDecls)
		{
			if (decl == ignoredDecl)
			{
				return true;
			}
		}
	}
	return false;
}

inline bool CxxNameResolver::ignoresContext(const clang::DeclContext* declContext) const
{
	if (const clang::Decl* decl = clang::dyn_cast_or_null<clang::Decl>(declContext))
	{
		return ignoresContext(decl);
	}
	return false;
}

inline CanonicalFilePathCache* CxxNameResolver::getCanonicalFilePathCache() const
{
	return m_canonicalFilePathCache;
}

inline const std::vector<const clang::Decl*>& CxxNameResolver::getIgnoredContextDecls() const
{
	return m_ignoredContextDecls;
}
