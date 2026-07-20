// Inline implementations for CxxLocationExtractor.h. Included via CxxAstVisitorBodies.h (classic) or the
// srctrl.cxx:visitor wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <optional>
#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>
#include "utilityClang.h"
#endif

inline CxxLocationExtractor::CxxLocationExtractor(
	clang::ASTContext& astContext,
	clang::Preprocessor* preprocessor,
	CanonicalFilePathCache& canonicalFilePathCache)
	: m_astContext(astContext)
	, m_preprocessor(preprocessor)
	, m_canonicalFilePathCache(canonicalFilePathCache)
{
}

inline ParseLocation CxxLocationExtractor::getParseLocation(const clang::SourceLocation& loc) const
{
	return utility::getParseLocation(
		loc, m_astContext.getSourceManager(), m_preprocessor, m_canonicalFilePathCache);
}

inline ParseLocation CxxLocationExtractor::getParseLocation(const clang::SourceRange& range) const
{
	return utility::getParseLocation(
		range, m_astContext.getSourceManager(), m_preprocessor, m_canonicalFilePathCache);
}

inline ParseLocation CxxLocationExtractor::getParseLocationOfTagDeclBody(clang::TagDecl* decl) const
{
	if (decl->isThisDeclarationADefinition())
	{
		clang::SourceRange range;
		if (clang::CXXRecordDecl* cxxDecl = clang::dyn_cast_or_null<clang::CXXRecordDecl>(decl))
		{
			clang::ClassTemplateDecl* templateDecl = cxxDecl->getDescribedClassTemplate();
			if (templateDecl)
			{
				range = templateDecl->getSourceRange();
			}
		}
		if (range.isInvalid())
		{
			range = decl->getDefinition()->getSourceRange();
		}
		return getParseLocation(range);
	}
	return ParseLocation();
}

inline ParseLocation CxxLocationExtractor::getParseLocationOfFunctionBody(const clang::FunctionDecl* decl) const
{
	if (decl->hasBody() && decl->isThisDeclarationADefinition())
	{
		clang::SourceRange range;
		clang::FunctionTemplateDecl* templateDecl = decl->getDescribedFunctionTemplate();
		if (templateDecl)
		{
			range = templateDecl->getSourceRange();
		}
		else
		{
			range = decl->getSourceRange();
		}
		return getParseLocation(range);
	}
	return ParseLocation();
}

inline ParseLocation CxxLocationExtractor::getSignatureLocation(clang::FunctionDecl* d) const
{
	clang::SourceRange signatureRange = d->getSourceRange();

	if (d->doesThisDeclarationHaveABody())
	{
		if (!d->getTypeSourceInfo())
		{
			return ParseLocation();
		}

		const clang::SourceManager& sm = m_astContext.getSourceManager();
		const clang::LangOptions& opts = m_astContext.getLangOpts();

		clang::SourceLocation endLoc = signatureRange.getBegin();

		if (d->getNumParams() > 0)
		{
			endLoc = d->getParamDecl(d->getNumParams() - 1)->getEndLoc();
		}

		while (sm.isBeforeInTranslationUnit(endLoc, signatureRange.getEnd()))
		{
			std::optional<clang::Token> token = clang::Lexer::findNextToken(endLoc, sm, opts);
			if (token.has_value())
			{
				const clang::tok::TokenKind tokenKind = token.value().getKind();
				if (tokenKind == clang::tok::l_brace || tokenKind == clang::tok::colon)
				{
					signatureRange.setEnd(endLoc);
					return getParseLocation(signatureRange);
				}

				clang::SourceLocation nextEndLoc = token.value().getLocation();
				if (nextEndLoc == endLoc)
				{
					return ParseLocation();
				}

				endLoc = nextEndLoc;
			}
			else
			{
				return ParseLocation();
			}
		}
		return ParseLocation();
	}

	return getParseLocation(signatureRange);
}
