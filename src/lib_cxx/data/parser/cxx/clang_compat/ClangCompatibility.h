#ifndef CLANG_COMPATIBILITY_H
#define CLANG_COMPATIBILITY_H

#include <memory>
#include <string>

namespace clang
{
class ASTConsumer;
class AutoType;
class CompilerInstance;
class ConceptDecl;
class ConceptReference;
class PCHBuffer;
}

namespace llvm::opt
{
class OptTable;
}

namespace clang_compat
{
const llvm::opt::OptTable& getDriverOptTable();

std::string getResourcesPath(const std::string& binaryPath);

std::unique_ptr<clang::ASTConsumer> createPchGenerator(
	clang::CompilerInstance& compilerInstance,
	const std::string& outputFile,
	const std::string& sysroot,
	const std::shared_ptr<clang::PCHBuffer>& buffer,
	bool allowAstWithErrors);

const clang::ConceptDecl* getTypeConstraintConceptDecl(const clang::AutoType* autoType);
const clang::ConceptDecl* getNamedConceptDecl(const clang::ConceptReference* conceptReference);
}

#endif	 // CLANG_COMPATIBILITY_H
