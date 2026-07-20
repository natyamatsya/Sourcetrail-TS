// Inline implementations for CxxTemplateArgumentNameResolver.h. Included via
// CxxNameResolverBodies.h (classic) or the srctrl.cxx:parser wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include "utilityClang.h"

#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/PrettyPrinter.h>
#endif


inline CxxTemplateArgumentNameResolver::CxxTemplateArgumentNameResolver(const CxxNameResolver* other)
	: CxxNameResolver(other)
{
}

inline std::string CxxTemplateArgumentNameResolver::getTemplateArgumentName(
	const clang::TemplateArgument& argument)
{
	// This doesn't work correctly if the template argument is dependent.
	// If that's required: build name from depth and index of template arg.
	const clang::TemplateArgument::ArgKind kind = argument.getKind();
	switch (kind)
	{
	case clang::TemplateArgument::Type:
	{
		CxxTypeNameResolver typeNameResolver(this);
		std::unique_ptr<CxxTypeName> typeName = CxxTypeName::makeUnsolvedIfNull(
			typeNameResolver.getName(argument.getAsType()));
		return typeName->toString();
	}
	case clang::TemplateArgument::Expression:
	{
		if (const auto* declRef = clang::dyn_cast<clang::DeclRefExpr>(argument.getAsExpr()))
			if (const auto* nttParm = clang::dyn_cast<clang::NonTypeTemplateParmDecl>(declRef->getDecl()))
				if (const clang::IdentifierInfo* id = nttParm->getIdentifier())
					return id->getName().str();
		[[fallthrough]];
	}
	case clang::TemplateArgument::Integral:
	case clang::TemplateArgument::Null:
	case clang::TemplateArgument::Declaration:
	case clang::TemplateArgument::NullPtr:
	case clang::TemplateArgument::Template:
	case clang::TemplateArgument::TemplateExpansion:	// handled correctly? template template parameter...
	case clang::TemplateArgument::StructuralValue:
	{
		clang::PrintingPolicy pp = utility::makePrintingPolicyForCPlusPlus();

		constexpr bool includeType = false;
		std::string buf;
		llvm::raw_string_ostream os(buf);
		argument.print(pp, os, includeType);
		return os.str();
	}
	case clang::TemplateArgument::Pack:
	{
		return "<...>";
	}
	}

	return "";
}
