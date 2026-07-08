#include "clang_compat/ClangCompat.h"

#if CLANG_VERSION_MAJOR >= 22

#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/NestedNameSpecifier.h>
#include <clang/AST/Type.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Options/OptionUtils.h>
#include <clang/Options/Options.h>
#include <clang/Serialization/ASTWriter.h>
#include <llvm/Support/Casting.h>

#include <optional>

namespace clang_compat
{
const llvm::opt::OptTable& getDriverOptTable()
{
	return clang::getDriverOptTable();
}

std::string getResourcesPath(const std::string& binaryPath)
{
	if (binaryPath.empty())
		return {};

	return clang::GetResourcesPath(binaryPath);
}

std::unique_ptr<clang::ASTConsumer> createPchGenerator(
	clang::CompilerInstance& compilerInstance,
	const std::string& outputFile,
	const std::string& sysroot,
	const std::shared_ptr<clang::PCHBuffer>& buffer,
	bool allowAstWithErrors)
{
	const auto& frontendOptions = compilerInstance.getFrontendOpts();

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
}

NestedNameSpecifierKind getNestedNameSpecifierKind(const NestedNameSpecifierRef nestedNameSpecifier)
{
	if (nestedNameSpecifier == clang::NestedNameSpecifier::getInvalid())
		return NestedNameSpecifierKind::Null;

	if (!nestedNameSpecifier)
		return NestedNameSpecifierKind::Null;

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

	return NestedNameSpecifierKind::Null;
}

const clang::NamedDecl* getNestedNameSpecifierNamespaceDecl(
	const NestedNameSpecifierRef nestedNameSpecifier)
{
	if (getNestedNameSpecifierKind(nestedNameSpecifier) != NestedNameSpecifierKind::Namespace)
		return nullptr;

	const auto namespaceAndPrefix = nestedNameSpecifier.getAsNamespaceAndPrefix();
	if (!namespaceAndPrefix.Namespace)
		return nullptr;

	return clang::dyn_cast_or_null<clang::NamedDecl>(namespaceAndPrefix.Namespace);
}

NestedNameSpecifierRef getNestedNameSpecifierPrefix(
	const NestedNameSpecifierRef nestedNameSpecifier)
{
	if (getNestedNameSpecifierKind(nestedNameSpecifier) != NestedNameSpecifierKind::Namespace)
		return std::nullopt;

	const auto namespaceAndPrefix = nestedNameSpecifier.getAsNamespaceAndPrefix();
	if (!namespaceAndPrefix.Namespace)
		return std::nullopt;

	return namespaceAndPrefix.Prefix;
}

const clang::Type* getNestedNameSpecifierType(const NestedNameSpecifierRef nestedNameSpecifier)
{
	if (getNestedNameSpecifierKind(nestedNameSpecifier) != NestedNameSpecifierKind::Type)
		return nullptr;

	return nestedNameSpecifier.getAsType();
}

const clang::CXXRecordDecl* getNestedNameSpecifierRecordDecl(
	const NestedNameSpecifierRef nestedNameSpecifier)
{
	const NestedNameSpecifierKind kind = getNestedNameSpecifierKind(nestedNameSpecifier);
	if (kind != NestedNameSpecifierKind::Type && kind != NestedNameSpecifierKind::MicrosoftSuper)
		return nullptr;

	return nestedNameSpecifier.getAsRecordDecl();
}

bool getNestedNameSpecifierLocPrefix(
	const clang::NestedNameSpecifierLoc& nestedNameSpecifierLoc,
	clang::NestedNameSpecifierLoc* const prefixOut)
{
	if (!prefixOut)
		return false;

	// Every specifier kind can have a prefix ("A::B::" -> "A::"); pre-22 clang
	// exposed it uniformly via getPrefix(). In 22+ a Namespace specifier carries
	// it alongside the namespace, while a Type specifier's prefix moved into the
	// type itself and is reached through its TypeLoc.
	switch (getNestedNameSpecifierKind(nestedNameSpecifierLoc.getNestedNameSpecifier()))
	{
	case NestedNameSpecifierKind::Namespace:
	{
		const auto namespaceAndPrefix = nestedNameSpecifierLoc.getAsNamespaceAndPrefix();
		if (!namespaceAndPrefix)
			return false;
		*prefixOut = namespaceAndPrefix.Prefix;
		return static_cast<bool>(*prefixOut);
	}
	case NestedNameSpecifierKind::Type:
		*prefixOut = nestedNameSpecifierLoc.castAsTypeLoc().getPrefix();
		return static_cast<bool>(*prefixOut);
	default:
		return false;
	}
}

clang::SourceLocation getNestedNameSpecifierLocalNameLoc(
	const clang::NestedNameSpecifierLoc& nestedNameSpecifierLoc)
{
	if (getNestedNameSpecifierKind(nestedNameSpecifierLoc.getNestedNameSpecifier()) ==
		NestedNameSpecifierKind::Type)
	{
		const clang::TypeLoc typeLoc = nestedNameSpecifierLoc.castAsTypeLoc();
		if (const auto tagLoc = typeLoc.getAs<clang::TagTypeLoc>())
			return tagLoc.getNameLoc();
		if (const auto typedefLoc = typeLoc.getAs<clang::TypedefTypeLoc>())
			return typedefLoc.getNameLoc();
		if (const auto usingLoc = typeLoc.getAs<clang::UsingTypeLoc>())
			return usingLoc.getNameLoc();
		if (const auto unresolvedLoc = typeLoc.getAs<clang::UnresolvedUsingTypeLoc>())
			return unresolvedLoc.getNameLoc();
		if (const auto templateLoc = typeLoc.getAs<clang::TemplateSpecializationTypeLoc>())
			return templateLoc.getTemplateNameLoc();
		if (const auto dependentLoc = typeLoc.getAs<clang::DependentNameTypeLoc>())
			return dependentLoc.getNameLoc();
		if (const auto parmLoc = typeLoc.getAs<clang::TemplateTypeParmTypeLoc>())
			return parmLoc.getNameLoc();
	}
	return nestedNameSpecifierLoc.getLocalBeginLoc();
}

const clang::ConceptDecl* getTypeConstraintConceptDecl(const clang::AutoType* const autoType)
{
	if (!autoType)
		return nullptr;

	return clang::dyn_cast_or_null<clang::ConceptDecl>(autoType->getTypeConstraintConcept());
}

const clang::ConceptDecl* getNamedConceptDecl(const clang::ConceptReference* const conceptReference)
{
	if (!conceptReference)
		return nullptr;

	return clang::dyn_cast_or_null<clang::ConceptDecl>(conceptReference->getNamedConcept());
}
}

#endif
