// Inline implementations for CxxAstVisitorComponentModuleIndexer.h. Included via CxxAstVisitorBodies.h (classic) or the
// srctrl.cxx:visitor wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/Basic/Module.h>
#include <clang/Lex/Lexer.h>

#include <optional>
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
	// Only a unit that builds the module's interface defines it: the primary interface and the
	// partitions (interface and implementation alike). An implementation unit (`module foo;`) just
	// imports foo's interface, which visitImportDecl records as that implicit import.
	const clang::Module* module = m_astContext->getCurrentNamedModule();
	if (module == nullptr || !module->isNamedModuleUnit() || !module->isInterfaceOrPartition())
	{
		return;
	}

	const Id moduleId = m_index.getOrCreateModuleSymbolId(module);
	m_index.recordDefinitionKind(moduleId, DefinitionKind::EXPLICIT);
	if (const clang::SourceRange name = getModuleNameRange(module->DefinitionLoc); name.isValid())
	{
		m_index.recordLocation(moduleId, m_index.getParseLocation(name), ParseLocationType::TOKEN);
	}
}

inline void CxxAstVisitorComponentModuleIndexer::visitImportDecl(clang::ImportDecl* d)
{
	// An import deserialized from another module's BMI is that module's own import: importing
	// `math` brings math's `export import :vec;` along, which is recorded once, where math.cppm is
	// indexed -- here it would read as this unit importing math's partitions.
	if (d->isFromASTFile() || !getAstVisitor()->shouldVisitReference(d->getLocation()))
	{
		return;
	}

	const clang::Module* imported = d->getImportedModule();
	if (imported == nullptr)
	{
		return;
	}

	// The location is the module name, as written (`import math;`), or for the implicit import of an
	// implementation unit, the name in its `module math;` declaration.
	const llvm::ArrayRef<clang::SourceLocation> nameLocations = d->getIdentifierLocs();
	const clang::SourceRange name = nameLocations.empty()
		? getModuleNameRange(d->getLocation())
		: clang::SourceRange{nameLocations.front(), nameLocations.back()};
	const ParseLocation location = m_index.getParseLocation(
		name.isValid() ? name : clang::SourceRange{d->getLocation(), d->getLocation()});

	// Source of the import edge: the module whose interface this unit builds, so the modules form a
	// graph of their own; otherwise -- an implementation unit or a plain translation unit -- the
	// importing file, the way an #include is recorded.
	const clang::Module* current = m_astContext->getCurrentNamedModule();
	const Id importingId = current != nullptr && current->isInterfaceOrPartition()
		? m_index.getOrCreateModuleSymbolId(current)
		: location.fileId;

	m_index.recordReference(
		ReferenceKind::IMPORT, m_index.getOrCreateModuleSymbolId(imported), importingId, location);
}

inline clang::SourceRange CxxAstVisitorComponentModuleIndexer::getModuleNameRange(
	clang::SourceLocation keywordLocation) const
{
	// The name follows the `module` keyword as identifiers joined by `.` (a dotted name) and `:` (a
	// partition): `module a.b:part;`. The AST keeps only the keyword's location, so lex on from it.
	if (keywordLocation.isInvalid() || keywordLocation.isMacroID())
	{
		return {};
	}
	const clang::SourceManager& sourceManager = m_astContext->getSourceManager();
	const clang::LangOptions& langOptions = m_astContext->getLangOpts();
	const auto next = [&](clang::SourceLocation location) {
		return clang::Lexer::findNextToken(location, sourceManager, langOptions);
	};

	const std::optional<clang::Token> first = next(keywordLocation);
	if (!first || !first->is(clang::tok::raw_identifier))
	{
		return {};
	}
	clang::SourceLocation last = first->getLocation();
	for (std::optional<clang::Token> separator = next(last);
		 separator && separator->isOneOf(clang::tok::period, clang::tok::colon);
		 separator = next(last))
	{
		const std::optional<clang::Token> part = next(separator->getLocation());
		if (!part || !part->is(clang::tok::raw_identifier))
		{
			break;
		}
		last = part->getLocation();
	}
	return {first->getLocation(), last};
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
