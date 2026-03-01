#include "Catch2.hpp"

#	include "CxxTypeName.h"

TEST_CASE("type name created with name has no qualifiers or modifiers")
{
	CxxTypeName typeName("int", std::vector<std::string>(), std::shared_ptr<CxxName>());
	REQUIRE("int" == typeName.toString());
}

TEST_CASE("type name created with name and const qualifier has no modifiers")
{
	CxxTypeName typeName("int", std::vector<std::string>(), std::shared_ptr<CxxName>());
	typeName.addQualifier(CxxQualifierFlags::QualifierType::CONST);
	REQUIRE("const int" == typeName.toString());
}

TEST_CASE("type name created with name and constexpr qualifier has no modifiers")
{
	CxxTypeName typeName("int", std::vector<std::string>(), std::shared_ptr<CxxName>());
	typeName.addQualifier(CxxQualifierFlags::QualifierType::CONSTEXPR);
	REQUIRE("constexpr int" == typeName.toString());
}

TEST_CASE("type name created with name and array modifier has array modifier")
{
	CxxTypeName typeName("int", std::vector<std::string>(), std::shared_ptr<CxxName>());
	typeName.addModifier(CxxTypeName::Modifier("[]"));
	REQUIRE("int []" == typeName.toString());
}

TEST_CASE("type name created with name and const pointer modifier has const pointer modifier")
{
	CxxTypeName typeName("int", std::vector<std::string>(), std::shared_ptr<CxxName>());
	typeName.addModifier(CxxTypeName::Modifier("*"));
	typeName.addQualifier(CxxQualifierFlags::QualifierType::CONST);
	REQUIRE("int * const" == typeName.toString());
}

TEST_CASE("type name created with name and pointer pointer modifier has pointer pointer modifier")
{
	CxxTypeName typeName("int", std::vector<std::string>(), std::shared_ptr<CxxName>());
	typeName.addModifier(CxxTypeName::Modifier("*"));
	typeName.addModifier(CxxTypeName::Modifier("*"));
	REQUIRE("int * *" == typeName.toString());
}
