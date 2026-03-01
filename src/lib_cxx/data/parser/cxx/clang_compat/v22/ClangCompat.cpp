#include "clang_compat/ClangCompat.h"

#if CLANG_VERSION_MAJOR >= 22

#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/NestedNameSpecifier.h>
#include <clang/AST/Type.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Options/OptionUtils.h>
#include <clang/Options/Options.h>
#include <clang/Serialization/ASTWriter.h>
#include <llvm/Support/Casting.h>

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

NestedNameSpecifierKind getNestedNameSpecifierKind(const clang::NestedNameSpecifier nestedNameSpecifier)
{
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
	const clang::NestedNameSpecifier nestedNameSpecifier)
{
	const auto namespaceAndPrefix = nestedNameSpecifier.getAsNamespaceAndPrefix();
	if (!namespaceAndPrefix.Namespace)
		return nullptr;

	return clang::dyn_cast_or_null<clang::NamedDecl>(namespaceAndPrefix.Namespace);
}

clang::NestedNameSpecifier getNestedNameSpecifierPrefix(
	const clang::NestedNameSpecifier nestedNameSpecifier)
{
	const auto namespaceAndPrefix = nestedNameSpecifier.getAsNamespaceAndPrefix();
	if (!namespaceAndPrefix.Namespace)
		return {};

	return namespaceAndPrefix.Prefix;
}

const clang::Type* getNestedNameSpecifierType(const clang::NestedNameSpecifier nestedNameSpecifier)
{
	return nestedNameSpecifier.getAsType();
}

const clang::CXXRecordDecl* getNestedNameSpecifierRecordDecl(
	const clang::NestedNameSpecifier nestedNameSpecifier)
{
	return nestedNameSpecifier.getAsRecordDecl();
}

bool getNestedNameSpecifierLocPrefix(
	const clang::NestedNameSpecifierLoc& nestedNameSpecifierLoc,
	clang::NestedNameSpecifierLoc* const prefixOut)
{
	if (!prefixOut)
		return false;

	const auto namespaceAndPrefix = nestedNameSpecifierLoc.getAsNamespaceAndPrefix();
	if (!namespaceAndPrefix)
		return false;

	*prefixOut = namespaceAndPrefix.Prefix;
	return static_cast<bool>(*prefixOut);
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
