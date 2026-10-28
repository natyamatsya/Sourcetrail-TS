// Inline implementations for CxxSymbolRegistry.h. Included via CxxAstVisitorBodies.h (classic) or the
// srctrl.cxx:visitor wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <clang/AST/Decl.h>
#include "CanonicalFilePathCache.h"
#include "CxxDeclNameResolver.h"
#include "CxxName.h"
#include "CxxTypeName.h"
#include "CxxTypeNameResolver.h"
#include "NameHierarchy.h"
#include "ParserClient.h"
#include "utilityMainFunction.h"
#endif

// ODR-safe home for the registry's helpers (see utilityMainFunction.inl).
namespace cxx_symbol_registry_detail
{
// The global main() that `decl` is declared in, if it is: what a lambda, a local class or enum, or
// a member of one, is declared in.
inline const clang::FunctionDecl* getEnclosingMainFunction(const clang::Decl& decl)
{
	for (const clang::DeclContext* context = decl.getDeclContext(); context;
		 context = context->getParent())
	{
		if (const auto* function = clang::dyn_cast<clang::FunctionDecl>(context);
			function && function->isMain())
		{
			return function;
		}
	}
	return nullptr;
}

// The global main() that the declaration a type names is declared in: its typedef's, else its
// tag's (class, enum, closure type) -- a typedef outside main() can name a type declared in it
// (std::decay_t<Local>).
inline const clang::FunctionDecl* getEnclosingMainFunction(const clang::Type& type)
{
	if (const auto* typedefType = type.getAs<clang::TypedefType>())
	{
		if (const clang::FunctionDecl* mainDecl = getEnclosingMainFunction(*typedefType->getDecl()))
		{
			return mainDecl;
		}
	}
	if (const clang::TagDecl* tagDecl = type.getAsTagDecl())
	{
		return getEnclosingMainFunction(*tagDecl);
	}
	return nullptr;
}
}	 // namespace cxx_symbol_registry_detail

inline CxxSymbolRegistry::CxxSymbolRegistry(
	ParserClient& client, CanonicalFilePathCache& canonicalFilePathCache)
	: m_client(client), m_canonicalFilePathCache(canonicalFilePathCache)
{
}

inline Id CxxSymbolRegistry::getOrCreateSymbolId(const clang::NamedDecl* decl)
{
	auto it = m_declSymbolIds.find(decl);
	if (it != m_declSymbolIds.end())
	{
		return it->second;
	}

	NameHierarchy symbolName("global", NameDelimiterType::UNKNOWN);
	if (decl)
	{
		CxxName declName = CxxDeclNameResolver(&m_canonicalFilePathCache).getName(decl);
		if (declName)
		{
			symbolName = declName.toNameHierarchy();

			if (clang::isa<const clang::FunctionDecl>(decl) && isMainFunction(symbolName))
			{
				uniquifyMainFunction(
					&symbolName, m_canonicalFilePathCache.getDeclarationFilePath(decl).str());
			}
			else if (isInMainFunction(symbolName))
			{
				uniquifyInMainFunction(
					symbolName, cxx_symbol_registry_detail::getEnclosingMainFunction(*decl));
			}
		}
	}

	Id symbolId = m_client.recordSymbol(symbolName);
	m_declSymbolIds.emplace(decl, symbolId);

	return symbolId;
}

inline Id CxxSymbolRegistry::getOrCreateSymbolId(const clang::Type* type)
{
	auto it = m_typeSymbolIds.find(type);
	if (it != m_typeSymbolIds.end())
	{
		return it->second;
	}

	NameHierarchy symbolName("global", NameDelimiterType::UNKNOWN);
	if (type)
	{
		std::unique_ptr<CxxTypeName> typeName = CxxTypeNameResolver(&m_canonicalFilePathCache)
													.getName(type);
		if (typeName)
		{
			symbolName = typeName->toNameHierarchy();

			if (isInMainFunction(symbolName))
			{
				uniquifyInMainFunction(
					symbolName, cxx_symbol_registry_detail::getEnclosingMainFunction(*type));
			}
		}
	}

	Id symbolId = m_client.recordSymbol(symbolName);
	m_typeSymbolIds.emplace(type, symbolId);

	return symbolId;
}

inline Id CxxSymbolRegistry::getOrCreateSymbolId(CxxContext context)
{
	if (context)
	{
		if (const clang::NamedDecl* decl = clang::dyn_cast<const clang::NamedDecl*>(context))
		{
			return getOrCreateSymbolId(decl);
		}
		return getOrCreateSymbolId(clang::cast<const clang::Type*>(context));
	}

	const clang::NamedDecl* decl {nullptr};
	return getOrCreateSymbolId(decl);
}

inline Id CxxSymbolRegistry::getOrCreateSymbolId(CxxContext context, const NameHierarchy& fallback)
{
	if (context)
	{
		if (const clang::NamedDecl* decl = clang::dyn_cast<const clang::NamedDecl*>(context))
		{
			return getOrCreateSymbolId(decl);
		}
		if (const clang::Type* type = clang::dyn_cast<const clang::Type*>(context))
		{
			return getOrCreateSymbolId(type);
		}
	}

	return m_client.recordSymbol(fallback);	   // TODO: cache result somehow
}

// main() is made unique per file (uniquifyMainFunction); a name declared in it is made unique the
// same way, with the file of the main() it is declared in (mainDecl, if found), so it is a child of
// that file's main() rather than of a main() node common to all files.
inline void CxxSymbolRegistry::uniquifyInMainFunction(
	NameHierarchy& symbolName, const clang::FunctionDecl* mainDecl)
{
	if (mainDecl)
	{
		uniquifyMainFunction(
			&symbolName, m_canonicalFilePathCache.getDeclarationFilePath(mainDecl).str());
	}
}
