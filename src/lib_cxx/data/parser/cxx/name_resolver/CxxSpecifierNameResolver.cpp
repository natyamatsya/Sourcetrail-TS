#include "CxxSpecifierNameResolver.h"

#include "CxxDeclNameResolver.h"
#include "CxxTypeNameResolver.h"
#include "clang_compat/ClangCompat.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/PrettyPrinter.h>

CxxSpecifierNameResolver::CxxSpecifierNameResolver(CanonicalFilePathCache* canonicalFilePathCache)
	: CxxNameResolver(canonicalFilePathCache)
{
}

CxxSpecifierNameResolver::CxxSpecifierNameResolver(const CxxNameResolver* other)
	: CxxNameResolver(other)
{
}

std::unique_ptr<CxxName> CxxSpecifierNameResolver::getName(
	clang_compat::NestedNameSpecifierRef nestedNameSpecifier)
{
	if (!nestedNameSpecifier)
		return nullptr;

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
			std::unique_ptr<CxxName> name = std::make_unique<CxxDeclName>(id->getName().str());

			std::unique_ptr<CxxName> parentName =
				getName(nestedNameSpecifier->getPrefix());
			if (parentName)
				name->setParent(std::move(parentName));

			return name;
		}
		break;
	}
#endif

	case clang_compat::NestedNameSpecifierKind::Namespace:
	{
		const clang::NamedDecl* namespaceDecl =
			clang_compat::getNestedNameSpecifierNamespaceDecl(nestedNameSpecifier);
		if (!namespaceDecl)
			return nullptr;

		std::unique_ptr<CxxName> name =
			CxxDeclNameResolver(this).getName(namespaceDecl);
		if (!name)
			return nullptr;

		std::unique_ptr<CxxName> parentName =
			getName(clang_compat::getNestedNameSpecifierPrefix(nestedNameSpecifier));
		if (parentName)
			name->setParent(std::move(parentName));

		return name;
	}

	case clang_compat::NestedNameSpecifierKind::Type:
		return CxxTypeName::makeUnsolvedIfNull(
			CxxTypeNameResolver(this).getName(
				clang_compat::getNestedNameSpecifierType(nestedNameSpecifier)));

	case clang_compat::NestedNameSpecifierKind::MicrosoftSuper:
		return CxxDeclNameResolver(this).getName(
			clang_compat::getNestedNameSpecifierRecordDecl(nestedNameSpecifier));
	}

	return nullptr;
}
