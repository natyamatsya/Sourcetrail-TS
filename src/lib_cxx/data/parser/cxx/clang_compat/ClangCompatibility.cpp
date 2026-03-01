#include "ClangCompatibility.h"

#include <clang/AST/DeclTemplate.h>
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
