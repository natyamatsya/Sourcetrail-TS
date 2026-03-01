#include "ClangCompatibility.h"

#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/NestedNameSpecifier.h>
#include <clang/AST/Type.h>
#include <clang/Basic/Version.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Serialization/ASTWriter.h>
#include <llvm/Support/Casting.h>

#if CLANG_VERSION_MAJOR >= 22
#include <clang/Options/OptionUtils.h>
#include <clang/Options/Options.h>
#else
#include <clang/Driver/Driver.h>
#include <clang/Driver/Options.h>
#endif

namespace clang_compat
{
const llvm::opt::OptTable& getDriverOptTable()
{
#if CLANG_VERSION_MAJOR >= 22
	return clang::getDriverOptTable();
#else
	return clang::driver::getDriverOptTable();
#endif
}

std::string getResourcesPath(const std::string& binaryPath)
{
	if (binaryPath.empty())
		return {};

#if CLANG_VERSION_MAJOR >= 22
	return clang::GetResourcesPath(binaryPath);
#else
	return clang::driver::Driver::GetResourcesPath(binaryPath);
#endif
}

std::unique_ptr<clang::ASTConsumer> createPchGenerator(
	clang::CompilerInstance& compilerInstance,
	const std::string& outputFile,
	const std::string& sysroot,
	const std::shared_ptr<clang::PCHBuffer>& buffer,
	bool allowAstWithErrors)
{
	const auto& frontendOptions = compilerInstance.getFrontendOpts();

#if CLANG_VERSION_MAJOR >= 22
	return std::make_unique<clang::PCHGenerator>(
		compilerInstance.getPreprocessor(),
		compilerInstance.getModuleCache(),
		outputFile,
		sysroot,
		buffer,
		compilerInstance.getCodeGenOpts(),
		frontendOptions.ModuleFileExtensions,
		allowAstWithErrors,
		frontendOptions.IncludeTimestamps,
		false,
		+compilerInstance.getLangOpts().CacheGeneratedPCH);
#else
	return std::make_unique<clang::PCHGenerator>(
		compilerInstance.getPreprocessor(),
		compilerInstance.getModuleCache(),
		outputFile,
		sysroot,
		buffer,
		frontendOptions.ModuleFileExtensions,
		allowAstWithErrors,
		frontendOptions.IncludeTimestamps,
		+compilerInstance.getLangOpts().CacheGeneratedPCH);
#endif
}

NestedNameSpecifierKind getNestedNameSpecifierKind(clang::NestedNameSpecifier nestedNameSpecifier)
{
	if (!nestedNameSpecifier)
	{
		return NestedNameSpecifierKind::Null;
	}

#if CLANG_VERSION_MAJOR >= 22
	switch (nestedNameSpecifier.getKind())
	{
	case clang::NestedNameSpecifier::Kind::Null:
		return NestedNameSpecifierKind::Null;
	case clang::NestedNameSpecifier::Kind::Global:
		return NestedNameSpecifierKind::Global;
	case clang::NestedNameSpecifier::Kind::Namespace:
		return NestedNameSpecifierKind::Namespace;
	case clang::NestedNameSpecifier::Kind::Type:
		return NestedNameSpecifierKind::Type;
	case clang::NestedNameSpecifier::Kind::MicrosoftSuper:
		return NestedNameSpecifierKind::MicrosoftSuper;
	}
#else
	switch (nestedNameSpecifier.getKind())
	{
	case clang::NestedNameSpecifier::Identifier:
	case clang::NestedNameSpecifier::TypeSpec:
	case clang::NestedNameSpecifier::TypeSpecWithTemplate:
		return NestedNameSpecifierKind::Type;
	case clang::NestedNameSpecifier::Namespace:
	case clang::NestedNameSpecifier::NamespaceAlias:
		return NestedNameSpecifierKind::Namespace;
	case clang::NestedNameSpecifier::Global:
		return NestedNameSpecifierKind::Global;
	case clang::NestedNameSpecifier::Super:
		return NestedNameSpecifierKind::MicrosoftSuper;
	}
#endif

	return NestedNameSpecifierKind::Null;
}

const clang::NamedDecl* getNestedNameSpecifierNamespaceDecl(
	clang::NestedNameSpecifier nestedNameSpecifier)
{
#if CLANG_VERSION_MAJOR >= 22
	const auto namespaceAndPrefix = nestedNameSpecifier.getAsNamespaceAndPrefix();
	if (!namespaceAndPrefix.Namespace)
	{
		return nullptr;
	}

	return clang::dyn_cast_or_null<clang::NamedDecl>(namespaceAndPrefix.Namespace);
#else
	if (const auto* namespaceDecl = nestedNameSpecifier.getAsNamespace())
	{
		return namespaceDecl;
	}

	if (const auto* namespaceAliasDecl = nestedNameSpecifier.getAsNamespaceAlias())
	{
		return namespaceAliasDecl;
	}

	return nullptr;
#endif
}

clang::NestedNameSpecifier getNestedNameSpecifierPrefix(
	clang::NestedNameSpecifier nestedNameSpecifier)
{
#if CLANG_VERSION_MAJOR >= 22
	const auto namespaceAndPrefix = nestedNameSpecifier.getAsNamespaceAndPrefix();
	if (namespaceAndPrefix.Namespace)
	{
		return namespaceAndPrefix.Prefix;
	}

	return {};
#else
	if (const auto* prefix = nestedNameSpecifier.getPrefix())
	{
		return *prefix;
	}

	return {};
#endif
}

const clang::Type* getNestedNameSpecifierType(clang::NestedNameSpecifier nestedNameSpecifier)
{
	return nestedNameSpecifier.getAsType();
}

const clang::CXXRecordDecl* getNestedNameSpecifierRecordDecl(
	clang::NestedNameSpecifier nestedNameSpecifier)
{
	return nestedNameSpecifier.getAsRecordDecl();
}

bool getNestedNameSpecifierLocPrefix(
	const clang::NestedNameSpecifierLoc& nestedNameSpecifierLoc,
	clang::NestedNameSpecifierLoc* prefixOut)
{
	if (!prefixOut)
	{
		return false;
	}

#if CLANG_VERSION_MAJOR >= 22
	const auto namespaceAndPrefix = nestedNameSpecifierLoc.getAsNamespaceAndPrefix();
	if (!namespaceAndPrefix)
	{
		return false;
	}

	*prefixOut = namespaceAndPrefix.Prefix;
	return static_cast<bool>(*prefixOut);
#else
	const clang::NestedNameSpecifierLoc prefix = nestedNameSpecifierLoc.getPrefix();
	if (!prefix)
	{
		return false;
	}

	*prefixOut = prefix;
	return true;
#endif
}

const clang::ConceptDecl* getTypeConstraintConceptDecl(const clang::AutoType* autoType)
{
	if (!autoType)
		return nullptr;

#if CLANG_VERSION_MAJOR >= 22
	return clang::dyn_cast_or_null<clang::ConceptDecl>(autoType->getTypeConstraintConcept());
#else
	return autoType->getTypeConstraintConcept();
#endif
}

const clang::ConceptDecl* getNamedConceptDecl(const clang::ConceptReference* conceptReference)
{
	if (!conceptReference)
		return nullptr;

#if CLANG_VERSION_MAJOR >= 22
	return clang::dyn_cast_or_null<clang::ConceptDecl>(conceptReference->getNamedConcept());
#else
	return conceptReference->getNamedConcept();
#endif
}
}
