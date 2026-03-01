#include "Catch2.hpp"

#include "ClangCompat.h"

#include <clang/AST/NestedNameSpecifier.h>
#include <clang/AST/Type.h>

#include <optional>

namespace
{
struct MockTypeVisitor
{
	bool called{false};
	bool traverseQualifierValue{false};
	bool returnValue{true};

#if CLANG_VERSION_MAJOR >= 22
	bool TraverseType(const clang::QualType& /*type*/, const bool traverseQualifier)
	{
		called = true;
		traverseQualifierValue = traverseQualifier;
		return returnValue;
	}
#else
	bool TraverseType(const clang::QualType& /*type*/)
	{
		called = true;
		return returnValue;
	}
#endif
};

struct MockTypeLocVisitor
{
	bool called{false};
	bool traverseQualifierValue{false};
	bool returnValue{true};

#if CLANG_VERSION_MAJOR >= 22
	bool TraverseTypeLoc(const clang::TypeLoc& /*typeLoc*/, const bool traverseQualifier)
	{
		called = true;
		traverseQualifierValue = traverseQualifier;
		return returnValue;
	}
#else
	bool TraverseTypeLoc(const clang::TypeLoc& /*typeLoc*/)
	{
		called = true;
		return returnValue;
	}
#endif
};
}

TEST_CASE("clang compat returns driver option table")
{
	const llvm::opt::OptTable* const optTable = &clang_compat::getDriverOptTable();
	REQUIRE(optTable != nullptr);
}

TEST_CASE("clang compat resource path returns empty for empty binary path")
{
	REQUIRE(clang_compat::getResourcesPath("").empty());
}

TEST_CASE("clang compat nested-name-specifier helpers handle empty values")
{
#if CLANG_VERSION_MAJOR >= 22
	const clang_compat::NestedNameSpecifierRef nestedNameSpecifier{std::nullopt};
#else
	const clang_compat::NestedNameSpecifierRef nestedNameSpecifier = nullptr;
#endif

	REQUIRE(
		clang_compat::getNestedNameSpecifierKind(nestedNameSpecifier) ==
		clang_compat::NestedNameSpecifierKind::Null);
	REQUIRE(clang_compat::getNestedNameSpecifierNamespaceDecl(nestedNameSpecifier) == nullptr);
	REQUIRE(!clang_compat::getNestedNameSpecifierPrefix(nestedNameSpecifier));
	REQUIRE(clang_compat::getNestedNameSpecifierType(nestedNameSpecifier) == nullptr);
	REQUIRE(clang_compat::getNestedNameSpecifierRecordDecl(nestedNameSpecifier) == nullptr);
}

TEST_CASE("clang compat nested-name-specifier-loc prefix helper handles empty values")
{
	const clang::NestedNameSpecifierLoc nestedNameSpecifierLoc{};
	clang::NestedNameSpecifierLoc prefix;

	REQUIRE(!clang_compat::getNestedNameSpecifierLocPrefix(nestedNameSpecifierLoc, &prefix));
	REQUIRE(!prefix);
	REQUIRE(!clang_compat::getNestedNameSpecifierLocPrefix(nestedNameSpecifierLoc, nullptr));
}

TEST_CASE("clang compat concept helpers return null for null inputs")
{
	REQUIRE(clang_compat::getTypeConstraintConceptDecl(nullptr) == nullptr);
	REQUIRE(clang_compat::getNamedConceptDecl(nullptr) == nullptr);
}

TEST_CASE("clang compat traverseType delegates to visitor")
{
	MockTypeVisitor visitor;
	visitor.returnValue = false;

	const bool result = clang_compat::traverseType(visitor, clang::QualType{}, false);
	REQUIRE(!result);
	REQUIRE(visitor.called);

#if CLANG_VERSION_MAJOR >= 22
	REQUIRE(!visitor.traverseQualifierValue);
#endif
}

TEST_CASE("clang compat traverseTypeLoc delegates to visitor")
{
	MockTypeLocVisitor visitor;
	visitor.returnValue = false;

	const bool result = clang_compat::traverseTypeLoc(visitor, clang::TypeLoc{}, true);
	REQUIRE(!result);
	REQUIRE(visitor.called);

#if CLANG_VERSION_MAJOR >= 22
	REQUIRE(visitor.traverseQualifierValue);
#endif
}
