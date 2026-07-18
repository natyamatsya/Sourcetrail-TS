#include "CxxAstVisitorComponentTypeIndexer.h"

#include "CxxAstVisitor.h"
#include "CxxAstVisitorComponentTypeRefKind.h"
#include "CxxIndexingContext.h"
#include "clang_compat/ClangCompat.h"
#include "utilityClang.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/Basic/SourceLocation.h>

using namespace std;
using namespace clang;

CxxAstVisitorComponentTypeIndexer::CxxAstVisitorComponentTypeIndexer(
	CxxAstVisitor* astVisitor, CxxIndexingContext& index)
	: CxxAstVisitorComponent(astVisitor)
	, m_index(index)
{
}

void CxxAstVisitorComponentTypeIndexer::wire()
{
	m_typeRefKind = getAstVisitor()->getTypeRefKindComponent();
}

void CxxAstVisitorComponentTypeIndexer::beginTraverseNestedNameSpecifierLoc(
	const clang::NestedNameSpecifierLoc& loc)
{
	if (!getAstVisitor()->shouldVisitReference(loc.getBeginLoc()))
	{
		return;
	}

	const auto nestedNameSpecifier = loc.getNestedNameSpecifier();
	switch (clang_compat::getNestedNameSpecifierKind(nestedNameSpecifier))
	{
	case clang_compat::NestedNameSpecifierKind::Null:
	case clang_compat::NestedNameSpecifierKind::Global:
	case clang_compat::NestedNameSpecifierKind::Identifier:
	case clang_compat::NestedNameSpecifierKind::MicrosoftSuper:
		break;

	case clang_compat::NestedNameSpecifierKind::Namespace:
	{
		const clang::NamedDecl* namespaceDecl =
			clang_compat::getNestedNameSpecifierNamespaceDecl(nestedNameSpecifier);
		if (!namespaceDecl)
			break;

		Id symbolId = m_index.getOrCreateSymbolId(namespaceDecl);
		m_index.recordSymbolKind(symbolId, SymbolKind::NAMESPACE);
		m_index.recordLocation(
			symbolId, m_index.getParseLocation(clang_compat::getNestedNameSpecifierLocalNameLoc(loc)), ParseLocationType::QUALIFIER);

		if (const auto* namespaceAliasDecl =
				clang::dyn_cast<clang::NamespaceAliasDecl>(namespaceDecl))
		{
			symbolId = m_index.getOrCreateSymbolId(namespaceAliasDecl->getAliasedNamespace());
			m_index.recordSymbolKind(symbolId, SymbolKind::NAMESPACE);
		}
	}
	break;

	case clang_compat::NestedNameSpecifierKind::Type:
		if (const clang::CXXRecordDecl* recordDecl =
				clang_compat::getNestedNameSpecifierRecordDecl(nestedNameSpecifier))
		{
			SymbolKind symbolKind = SymbolKind::UNDEFINED;
			if (recordDecl->isClass())
			{
				symbolKind = SymbolKind::CLASS;
			}
			else if (recordDecl->isStruct())
			{
				symbolKind = SymbolKind::STRUCT;
			}
			else if (recordDecl->isUnion())
			{
				symbolKind = SymbolKind::UNION;
			}

			if (symbolKind != SymbolKind::UNDEFINED)
			{
				const Id symbolId = m_index.getOrCreateSymbolId(recordDecl);
				m_index.recordSymbolKind(symbolId, symbolKind);
				m_index.recordLocation(
					symbolId, m_index.getParseLocation(clang_compat::getNestedNameSpecifierLocalNameLoc(loc)), ParseLocationType::QUALIFIER);
			}
		}
		else if (
			const clang::Type* type = clang_compat::getNestedNameSpecifierType(nestedNameSpecifier))
		{
			const ParseLocation parseLocation = m_index.getParseLocation(clang_compat::getNestedNameSpecifierLocalNameLoc(loc));

			if (const clang::TemplateTypeParmType* tpt =
					clang::dyn_cast_or_null<clang::TemplateTypeParmType>(type))
			{
				clang::TemplateTypeParmDecl* d = tpt->getDecl();
				if (d)
				{
					m_index.recordLocalSymbol(m_index.getLocalSymbolName(d->getLocation()), parseLocation);
				}
			}
			else
			{
				const Id symbolId = m_index.getOrCreateSymbolId(type);
				m_index.recordLocation(symbolId, parseLocation, ParseLocationType::QUALIFIER);
			}
		}
		break;
	}
}

void CxxAstVisitorComponentTypeIndexer::beginTraverseTemplateArgumentLoc(
	const clang::TemplateArgumentLoc& loc)
{
	if (getAstVisitor()->shouldVisitReference(loc.getLocation()))
	{
		if (loc.getArgument().getKind() == clang::TemplateArgument::Template)
		{
			// TODO: maybe move this to VisitTemplateName

			const clang::TemplateName templateTemplateArgumentName = loc.getArgument().getAsTemplate();

			const ParseLocation parseLocation = m_index.getParseLocation(loc.getLocation());
			if (templateTemplateArgumentName.isDependent())
			{
				clang::SourceLocation declLocation;
				if (templateTemplateArgumentName.getAsTemplateDecl())
				{
					declLocation = templateTemplateArgumentName.getAsTemplateDecl()->getLocation();
				}
				else
				{
					declLocation = loc.getLocation();
				}
				m_index.recordLocalSymbol(m_index.getLocalSymbolName(declLocation), parseLocation);
			}
			else
			{
				const Id symbolId = m_index.getOrCreateSymbolId(
					templateTemplateArgumentName.getAsTemplateDecl());

				m_index.recordReference(
					ReferenceKind::TYPE_USAGE,
					symbolId,
					m_index.getOrCreateSymbolId(
						m_index.getContext()),
					parseLocation);

				{
					if (const clang::NamedDecl* namedContextDecl =
							m_index.getTopmostContextDecl(1))
					{
						m_index.recordReference(
							ReferenceKind::TYPE_USAGE,
							symbolId,
							m_index.getOrCreateSymbolId(namedContextDecl),	  // we use the closest named decl
																	  // here (e.g. function decl)
							parseLocation);
					}
				}
			}
		}
	}
}

void CxxAstVisitorComponentTypeIndexer::beginTraverseLambdaCapture(
	clang::LambdaExpr* lambdaExpr, const clang::LambdaCapture* capture)
{
	if ((!lambdaExpr->isInitCapture(capture)) && (capture->capturesVariable()))
	{
		clang::ValueDecl* d = capture->getCapturedVar();
		if (utility::isLocalVariable(d) || utility::isParameter(d))
		{
			if (!d->getNameAsString().empty())	  // don't record anonymous parameters
			{
				m_index.recordLocalSymbol(
					m_index.getLocalSymbolName(d->getLocation()), m_index.getParseLocation(capture->getLocation()));
			}
		}
	}
}

void CxxAstVisitorComponentTypeIndexer::visitTypeLoc(clang::TypeLoc tl)
{
	if (tl.isNull())
	{
		return;
	}

	if ((getAstVisitor()->shouldVisitReference(tl.getBeginLoc())) &&
		(getAstVisitor()->shouldHandleTypeLoc(tl)))
	{
		if (const clang::TemplateTypeParmTypeLoc &ttptl = tl.getAs<clang::TemplateTypeParmTypeLoc>())
		{
			clang::TemplateTypeParmDecl* decl = ttptl.getDecl();
			if (decl)
			{
				m_index.recordLocalSymbol(
					m_index.getLocalSymbolName(decl->getLocation()), m_index.getParseLocation(tl.getBeginLoc()));
			}
		}
		else
		{
			if (const clang::TemplateSpecializationTypeLoc& tstl = tl.getAs<clang::TemplateSpecializationTypeLoc>())
			{
				const clang::TemplateSpecializationType* tst = tstl.getTypePtr();
				if (tst)
				{
					const clang::TemplateName tln = tst->getTemplateName();
					// T<int> where T is a template template PARAMETER is a local
					// symbol of the enclosing template, not a type usage. Other
					// dependent template names (a member template of a dependent
					// base, pre-22 a separate DependentTemplateSpecializationType)
					// resolve to a real symbol and fall through to the recording.
					if (tln.isDependent() &&
						clang::isa_and_nonnull<clang::TemplateTemplateParmDecl>(
							tln.getAsTemplateDecl()))
					{
						m_index.recordLocalSymbol(
							m_index.getLocalSymbolName(tln.getAsTemplateDecl()->getLocation()),
							m_index.getParseLocation(tl.getBeginLoc()));
						return;
					}
				}
			}

			const Id symbolId = m_index.getOrCreateSymbolId(tl.getTypePtr());

			if (clang::dyn_cast_or_null<clang::BuiltinType>(tl.getTypePtr()))
			{
				m_index.recordSymbolKind(symbolId, SymbolKind::BUILTIN_TYPE);
				m_index.recordDefinitionKind(symbolId, DefinitionKind::EXPLICIT);
			}

			// The type's own name token: in LLVM 22+ the TypeLoc includes any
			// qualifier ("test::TestStruct"), so getBeginLoc() would point at the
			// qualifier instead of the name and misplace the type-use location.
			const clang::SourceLocation loc = clang_compat::getTypeLocNameLocation(tl);

			const ParseLocation parseLocation = m_index.getParseLocation(loc);

			m_index.recordReference(m_typeRefKind->isTraversingInheritance() ? ReferenceKind::INHERITANCE : ReferenceKind::TYPE_USAGE,
				symbolId, m_index.getOrCreateSymbolId(m_index.getContext(1)),	// we skip the last element because it refers to this typeloc.
				parseLocation);

			if (m_typeRefKind->isTraversingTemplateArgument())
			{
				if (const clang::NamedDecl* namedContextDecl = m_index.getTopmostContextDecl(2))
				{
					m_index.recordReference(
						ReferenceKind::TYPE_USAGE,
						symbolId,
						m_index.getOrCreateSymbolId(namedContextDecl),	  // we use the closest named decl here
						parseLocation);
				}
			}
		}
	}
}
