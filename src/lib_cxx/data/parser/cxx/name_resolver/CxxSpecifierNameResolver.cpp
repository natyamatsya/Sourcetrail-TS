#include "CxxSpecifierNameResolver.h"

#include "CxxDeclNameResolver.h"
#include "CxxTypeNameResolver.h"

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
	clang::NestedNameSpecifier nestedNameSpecifier)
{
	if (!nestedNameSpecifier)
		return nullptr;

	switch (nestedNameSpecifier.getKind())
	{
	case clang::NestedNameSpecifier::Kind::Null:
	case clang::NestedNameSpecifier::Kind::Global:
		// no context name hierarchy needed.
		break;

	case clang::NestedNameSpecifier::Kind::Namespace:
	{
		const auto namespaceAndPrefix = nestedNameSpecifier.getAsNamespaceAndPrefix();
		if (!namespaceAndPrefix.Namespace)
			return nullptr;

		std::unique_ptr<CxxName> name =
			CxxDeclNameResolver(this).getName(namespaceAndPrefix.Namespace);
		if (!name)
			return nullptr;

		std::unique_ptr<CxxName> parentName = getName(namespaceAndPrefix.Prefix);
		if (parentName)
			name->setParent(std::move(parentName));

		return name;
	}

	case clang::NestedNameSpecifier::Kind::Type:
		return CxxTypeName::makeUnsolvedIfNull(
			CxxTypeNameResolver(this).getName(nestedNameSpecifier.getAsType()));

	case clang::NestedNameSpecifier::Kind::MicrosoftSuper:
		return CxxDeclNameResolver(this).getName(nestedNameSpecifier.getAsRecordDecl());
	}

	return nullptr;
}
