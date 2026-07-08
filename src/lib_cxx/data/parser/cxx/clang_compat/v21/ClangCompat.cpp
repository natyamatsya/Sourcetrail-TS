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

NestedNameSpecifierKind getNestedNameSpecifierKind(const NestedNameSpecifierRef nestedNameSpecifier)
{
	if (!nestedNameSpecifier)
		return NestedNameSpecifierKind::Null;

	switch (nestedNameSpecifier->getKind())
	{
	case clang::NestedNameSpecifier::Identifier:
		return NestedNameSpecifierKind::Identifier;
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
	const NestedNameSpecifierRef nestedNameSpecifier)
{
	if (getNestedNameSpecifierKind(nestedNameSpecifier) != NestedNameSpecifierKind::Namespace)
		return nullptr;

	if (const auto* namespaceDecl = nestedNameSpecifier->getAsNamespace())
		return namespaceDecl;

	if (const auto* namespaceAliasDecl = nestedNameSpecifier->getAsNamespaceAlias())
		return namespaceAliasDecl;

	return nullptr;
}

NestedNameSpecifierRef getNestedNameSpecifierPrefix(
	const NestedNameSpecifierRef nestedNameSpecifier)
{
	if (getNestedNameSpecifierKind(nestedNameSpecifier) != NestedNameSpecifierKind::Namespace)
		return nullptr;

	return nestedNameSpecifier->getPrefix();
}

const clang::Type* getNestedNameSpecifierType(const NestedNameSpecifierRef nestedNameSpecifier)
{
	if (getNestedNameSpecifierKind(nestedNameSpecifier) != NestedNameSpecifierKind::Type)
		return nullptr;

	return nestedNameSpecifier->getAsType();
}

const clang::CXXRecordDecl* getNestedNameSpecifierRecordDecl(
	const NestedNameSpecifierRef nestedNameSpecifier)
{
	const NestedNameSpecifierKind kind = getNestedNameSpecifierKind(nestedNameSpecifier);
	if (kind != NestedNameSpecifierKind::Type && kind != NestedNameSpecifierKind::MicrosoftSuper)
		return nullptr;

	return nestedNameSpecifier->getAsRecordDecl();
}

bool getNestedNameSpecifierLocPrefix(
	const clang::NestedNameSpecifierLoc& nestedNameSpecifierLoc,
	clang::NestedNameSpecifierLoc* const prefixOut)
{
	if (!prefixOut)
		return false;

	// Every specifier kind can have a prefix ("A::B::" -> "A::"); pre-22 clang
	// exposes it uniformly via getPrefix(). Restricting the walk to Namespace
	// specifiers dropped the qualifier records of class/struct prefixes.
	const clang::NestedNameSpecifierLoc prefix = nestedNameSpecifierLoc.getPrefix();
	if (!prefix)
		return false;

	*prefixOut = prefix;
	return true;
}

clang::SourceLocation getNestedNameSpecifierLocalNameLoc(
	const clang::NestedNameSpecifierLoc& nestedNameSpecifierLoc)
{
	// Pre-22, the local range of a specifier already excludes its prefix.
	return nestedNameSpecifierLoc.getLocalBeginLoc();
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
