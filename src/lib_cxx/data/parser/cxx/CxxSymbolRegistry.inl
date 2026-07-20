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
