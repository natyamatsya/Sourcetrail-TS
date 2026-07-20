// Inline implementations for CxxSpecifierNameResolver.h. Included via CxxNameResolverBodies.h
// (classic) or the srctrl.cxx:parser wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include "clang_compat/ClangCompat.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/PrettyPrinter.h>
#endif

inline CxxSpecifierNameResolver::CxxSpecifierNameResolver(CanonicalFilePathCache* canonicalFilePathCache)
	: CxxNameResolver(canonicalFilePathCache)
{
}

inline CxxSpecifierNameResolver::CxxSpecifierNameResolver(const CxxNameResolver* other)
	: CxxNameResolver(other)
{
}

inline CxxName CxxSpecifierNameResolver::getName(
	clang_compat::NestedNameSpecifierRef nestedNameSpecifier)
{
	if (!nestedNameSpecifier)
		return {};

	switch (clang_compat::getNestedNameSpecifierKind(nestedNameSpecifier))
	{
	case clang_compat::NestedNameSpecifierKind::Null:
	case clang_compat::NestedNameSpecifierKind::Global:
		// no context name hierarchy needed.
		break;

#if LLVM_VERSION_MAJOR < 22
	case clang_compat::NestedNameSpecifierKind::Identifier:
	{
		if (const clang::IdentifierInfo* id = nestedNameSpecifier->getAsIdentifier())
		{
			CxxName name = CxxName::make<CxxDeclName>(id->getName().str());

			CxxName parentName = getName(nestedNameSpecifier->getPrefix());
			if (parentName)
				name.setParent(std::move(parentName));

			return name;
		}
		break;
	}
#else
	case clang_compat::NestedNameSpecifierKind::Identifier:
		// Pre-22 specifier kind; cannot occur with Clang 22+.
		break;
#endif

	case clang_compat::NestedNameSpecifierKind::Namespace:
	{
		const clang::NamedDecl* namespaceDecl =
			clang_compat::getNestedNameSpecifierNamespaceDecl(nestedNameSpecifier);
		if (!namespaceDecl)
			return {};

		CxxName name = CxxDeclNameResolver(this).getName(namespaceDecl);
		if (!name)
			return {};

		CxxName parentName =
			getName(clang_compat::getNestedNameSpecifierPrefix(nestedNameSpecifier));
		if (parentName)
			name.setParent(std::move(parentName));

		return name;
	}

	case clang_compat::NestedNameSpecifierKind::Type:
		return CxxName::make<CxxTypeName>(std::move(*CxxTypeName::makeUnsolvedIfNull(
			CxxTypeNameResolver(this).getName(
				clang_compat::getNestedNameSpecifierType(nestedNameSpecifier)))));

	case clang_compat::NestedNameSpecifierKind::MicrosoftSuper:
		return CxxDeclNameResolver(this).getName(
			clang_compat::getNestedNameSpecifierRecordDecl(nestedNameSpecifier));
	}

	return {};
}
