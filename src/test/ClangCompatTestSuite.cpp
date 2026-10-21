#include "Catch2.hpp"

#include "ClangCompat.h"

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/NestedNameSpecifier.h>
#include <clang/AST/Type.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Serialization/ASTWriter.h>
#include <clang/Tooling/Tooling.h>

#include <memory>
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

// Feeds a header through the clang_compat PCH generator alone (no container writer), so the test
// sees exactly what the version-specific PCHGenerator constructor call produced.
class PchBufferAction: public clang::ASTFrontendAction
{
public:
	explicit PchBufferAction(std::shared_ptr<clang::PCHBuffer> buffer): m_buffer(std::move(buffer)) {}

	std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
		clang::CompilerInstance& compilerInstance, llvm::StringRef /*inFile*/) override
	{
		return clang_compat::createPchGenerator(compilerInstance, "compat.pch", "", m_buffer, false);
	}

	clang::TranslationUnitKind getTranslationUnitKind() override
	{
		return clang::TU_Prefix;
	}

private:
	const std::shared_ptr<clang::PCHBuffer> m_buffer;
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

TEST_CASE("clang compat PCH generator serializes a header")
{
	const auto buffer = std::make_shared<clang::PCHBuffer>();

	const bool success = clang::tooling::runToolOnCodeWithArgs(
		std::make_unique<PchBufferAction>(buffer),
		"struct CompatPchStruct { int member; };",
		{"-x", "c++-header", "-std=c++17"},
		"compat.h");

	REQUIRE(success);
	REQUIRE(buffer->IsComplete);
	REQUIRE(!buffer->Data.empty());
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
