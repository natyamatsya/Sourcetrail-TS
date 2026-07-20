// Inline implementations for CxxAstVisitorComponentModuleIndexer.h. Included via CxxAstVisitorBodies.h (classic) or the
// srctrl.cxx:visitor wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/Basic/Module.h>
#include "CxxAstVisitor.h"
#include "CxxIndexingContext.h"
#include "DefinitionKind.h"
#include "NodeModifier.h"
#include "ParseLocation.h"
#include "ReferenceKind.h"
#include "types.h"
#endif

inline CxxAstVisitorComponentModuleIndexer::CxxAstVisitorComponentModuleIndexer(
	CxxAstVisitor* astVisitor, clang::ASTContext* astContext, CxxIndexingContext& index)
	: CxxAstVisitorComponent(astVisitor)
	, m_astContext(astContext)
	, m_index(index)
{
}

inline void CxxAstVisitorComponentModuleIndexer::visitTranslationUnitDecl(clang::TranslationUnitDecl* /*d*/)
{
	const clang::Module* module = m_astContext->getCurrentNamedModule();
	if (module == nullptr || !module->isNamedModuleUnit())
	{
		return;
	}

	const Id moduleId = m_index.getOrCreateModuleSymbolId(module);
	m_index.recordDefinitionKind(moduleId, DefinitionKind::EXPLICIT);
	if (module->DefinitionLoc.isValid())
	{
		m_index.recordLocation(
			moduleId, m_index.getParseLocation(module->DefinitionLoc), ParseLocationType::TOKEN);
	}
}

inline void CxxAstVisitorComponentModuleIndexer::visitImportDecl(clang::ImportDecl* d)
{
	if (!getAstVisitor()->shouldVisitReference(d->getLocation()))
	{
		return;
	}

	clang::Module* imported = d->getImportedModule();
	if (imported == nullptr)
	{
		return;
	}

	const Id importedId = m_index.getOrCreateModuleSymbolId(imported);

	// Source of the import edge: the module this TU defines, else the file/TU context.
	Id contextId;
	if (const clang::Module* current = m_astContext->getCurrentNamedModule())
	{
		contextId = m_index.getOrCreateModuleSymbolId(current);
	}
	else
	{
		contextId = m_index.contextSymbolId();
	}

	m_index.recordReference(
		ReferenceKind::IMPORT, importedId, contextId, m_index.getParseLocation(d->getLocation()));
}

inline void CxxAstVisitorComponentModuleIndexer::visitExportDecl(clang::ExportDecl* d)
{
	// Flag the declarations directly under this `export` region. (Grandchildren --
	// e.g. names inside an `export namespace N { ... }` -- are a later refinement.)
	for (const clang::Decl* child: d->decls())
	{
		if (const clang::NamedDecl* named = clang::dyn_cast<clang::NamedDecl>(child))
		{
			m_index.recordNodeModifier(m_index.getOrCreateSymbolId(named), NODE_MODIFIER_EXPORTED);
		}
	}
}
