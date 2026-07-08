#ifndef CLANG_COMPAT_H
#define CLANG_COMPAT_H

#include <clang/Basic/SourceLocation.h>
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
	Identifier,
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

// The location of a type's own name token. Pre-22 a TypeLoc never includes a
// qualifier (that lived in the separately-traversed ElaboratedTypeLoc), so the
// begin location is the name; in 22+ the qualifier is part of the TypeLoc and
// the name must be resolved per TypeLoc class.
clang::SourceLocation getTypeLocNameLocation(const clang::TypeLoc& typeLoc);

// The location of the specifier's own name token, excluding its prefix.
// In LLVM 22+ a Type specifier's TypeLoc spans the whole qualified-id (the
// prefix moved inside the type), so getLocalBeginLoc() would point at the
// outermost qualifier instead of the specifier's name.
clang::SourceLocation getNestedNameSpecifierLocalNameLoc(
	const clang::NestedNameSpecifierLoc& nestedNameSpecifierLoc);

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
