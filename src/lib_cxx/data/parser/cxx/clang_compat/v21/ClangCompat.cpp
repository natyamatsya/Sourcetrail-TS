#include "clang_compat/ClangCompat.h"

#if CLANG_VERSION_MAJOR < 22

#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/NestedNameSpecifier.h>
#include <clang/AST/Type.h>
#include <clang/Driver/Driver.h>
#include <clang/Driver/Options.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Serialization/ASTWriter.h>

namespace clang_compat
{
const llvm::opt::OptTable& getDriverOptTable()
{
	return clang::driver::getDriverOptTable();
}

std::string getResourcesPath(const std::string& binaryPath)
{
	if (binaryPath.empty())
		return {};

	return clang::driver::Driver::GetResourcesPath(binaryPath);
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
		frontendOptions.ModuleFileExtensions,
		allowAstWithErrors,
		frontendOptions.IncludeTimestamps,
		+compilerInstance.getLangOpts().CacheGeneratedPCH);
}

NestedNameSpecifierKind getNestedNameSpecifierKind(const clang::NestedNameSpecifier nestedNameSpecifier)
{
	if (!nestedNameSpecifier)
		return NestedNameSpecifierKind::Null;

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

	return NestedNameSpecifierKind::Null;
}

const clang::NamedDecl* getNestedNameSpecifierNamespaceDecl(
	const clang::NestedNameSpecifier nestedNameSpecifier)
{
	if (const auto* namespaceDecl = nestedNameSpecifier.getAsNamespace())
		return namespaceDecl;

	if (const auto* namespaceAliasDecl = nestedNameSpecifier.getAsNamespaceAlias())
		return namespaceAliasDecl;

	return nullptr;
}

clang::NestedNameSpecifier getNestedNameSpecifierPrefix(
	const clang::NestedNameSpecifier nestedNameSpecifier)
{
	if (const auto* prefix = nestedNameSpecifier.getPrefix())
		return *prefix;

	return {};
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

	const clang::NestedNameSpecifierLoc prefix = nestedNameSpecifierLoc.getPrefix();
	if (!prefix)
		return false;

	*prefixOut = prefix;
	return true;
}

const clang::ConceptDecl* getTypeConstraintConceptDecl(const clang::AutoType* const autoType)
{
	if (!autoType)
		return nullptr;

	return autoType->getTypeConstraintConcept();
}

const clang::ConceptDecl* getNamedConceptDecl(const clang::ConceptReference* const conceptReference)
{
	if (!conceptReference)
		return nullptr;

	return conceptReference->getNamedConcept();
}
}

#endif
