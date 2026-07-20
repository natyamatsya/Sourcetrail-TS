// Inline implementations for CxxConceptReferenceRecorder.h. Included via CxxAstVisitorBodies.h (classic) or the
// srctrl.cxx:visitor wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <clang/AST/ASTConcept.h>
#include <clang/AST/DeclTemplate.h>
#include "CxxAstVisitorComponentContext.h"
#include "CxxLocationExtractor.h"
#include "CxxSymbolRegistry.h"
#include "ParseLocation.h"
#include "ParserClient.h"
#include "ReferenceKind.h"
#include "clang_compat/ClangCompat.h"
#include "types.h"
#endif

inline CxxConceptReferenceRecorder::CxxConceptReferenceRecorder(
	ParserClient& client,
	CxxSymbolRegistry& symbols,
	CxxLocationExtractor& locations,
	CxxAstVisitorComponentContext& context)
	: m_client(client), m_symbols(symbols), m_locations(locations), m_context(context)
{
}

inline void CxxConceptReferenceRecorder::recordTemplateParameterConceptReferences(
	const clang::TemplateDecl* templateDecl)
{
	if (const clang::TemplateParameterList* templateParameters = templateDecl->getTemplateParameters())
	{
		for (const clang::NamedDecl* namedDecl : *templateParameters)
		{
			if (const clang::TemplateTypeParmDecl* templateTypeParmDecl =
					clang::dyn_cast<clang::TemplateTypeParmDecl>(namedDecl))
			{
				if (const clang::TypeConstraint* typeConstraint =
						templateTypeParmDecl->getTypeConstraint())
				{
					recordConceptReference(typeConstraint);
				}
			}
		}
	}
}

inline void CxxConceptReferenceRecorder::recordNamedConceptReference(
	const clang::ConceptReference* conceptReference)
{
	if (const auto* conceptDecl = clang_compat::getNamedConceptDecl(conceptReference))
	{
		const Id conceptDeclId = m_symbols.getOrCreateSymbolId(conceptDecl);
		const Id contextSymbolId = m_symbols.getOrCreateSymbolId(m_context.getContext());
		const ParseLocation conceptNameLocation =
			m_locations.getParseLocation(conceptReference->getLocation());

		m_client.recordReference(
			ReferenceKind::USAGE, conceptDeclId, contextSymbolId, conceptNameLocation);
	}
}
