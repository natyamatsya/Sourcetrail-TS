#ifndef CLANG_COMPAT_H
#define CLANG_COMPAT_H

#include <clang/Basic/Version.h>

#include <memory>
#include <string>

namespace clang
{
class ASTConsumer;
class AutoType;
class CompilerInstance;
class ConceptDecl;
class ConceptReference;
class CXXRecordDecl;
class NamedDecl;
class NestedNameSpecifier;
class NestedNameSpecifierLoc;
class PCHBuffer;
class QualType;
class Type;
class TypeLoc;
}

namespace clang_compat
{
#if CLANG_VERSION_MAJOR >= 22
// In LLVM 22+, NestedNameSpecifier is a value type.
using NestedNameSpecifierRef = clang::NestedNameSpecifier;
#else
// In LLVM <22, NestedNameSpecifier is a heap-allocated class used via pointer.
using NestedNameSpecifierRef = clang::NestedNameSpecifier*;
#endif
}

namespace llvm::opt
{
class OptTable;
}

namespace clang_compat
{
enum class NestedNameSpecifierKind
{
	Null,
	Global,
	Namespace,
	Type,
	MicrosoftSuper
};

const llvm::opt::OptTable& getDriverOptTable();

std::string getResourcesPath(const std::string& binaryPath);

std::unique_ptr<clang::ASTConsumer> createPchGenerator(
	clang::CompilerInstance& compilerInstance,
	const std::string& outputFile,
	const std::string& sysroot,
	const std::shared_ptr<clang::PCHBuffer>& buffer,
	bool allowAstWithErrors);

NestedNameSpecifierKind getNestedNameSpecifierKind(NestedNameSpecifierRef nestedNameSpecifier);

const clang::NamedDecl* getNestedNameSpecifierNamespaceDecl(
	NestedNameSpecifierRef nestedNameSpecifier);

NestedNameSpecifierRef getNestedNameSpecifierPrefix(
	NestedNameSpecifierRef nestedNameSpecifier);

const clang::Type* getNestedNameSpecifierType(NestedNameSpecifierRef nestedNameSpecifier);

const clang::CXXRecordDecl* getNestedNameSpecifierRecordDecl(
	NestedNameSpecifierRef nestedNameSpecifier);

bool getNestedNameSpecifierLocPrefix(
	const clang::NestedNameSpecifierLoc& nestedNameSpecifierLoc,
	clang::NestedNameSpecifierLoc* prefixOut);

const clang::ConceptDecl* getTypeConstraintConceptDecl(const clang::AutoType* autoType);
const clang::ConceptDecl* getNamedConceptDecl(const clang::ConceptReference* conceptReference);

template <typename BaseVisitorT>
bool traverseType(
	BaseVisitorT& baseVisitor,
	const clang::QualType& type,
	bool traverseQualifier)
{
#if CLANG_VERSION_MAJOR >= 22
	return baseVisitor.TraverseType(type, traverseQualifier);
#else
	(void)traverseQualifier;
	return baseVisitor.TraverseType(type);
#endif
}

template <typename BaseVisitorT>
bool traverseTypeLoc(
	BaseVisitorT& baseVisitor,
	const clang::TypeLoc& typeLoc,
	bool traverseQualifier)
{
#if CLANG_VERSION_MAJOR >= 22
	return baseVisitor.TraverseTypeLoc(typeLoc, traverseQualifier);
#else
	(void)traverseQualifier;
	return baseVisitor.TraverseTypeLoc(typeLoc);
#endif
}
}

#endif	 // CLANG_COMPAT_H
