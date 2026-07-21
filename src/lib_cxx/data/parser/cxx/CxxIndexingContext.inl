// Inline implementations for CxxIndexingContext.h. Included via CxxAstVisitorBodies.h (classic) or the
// srctrl.cxx:visitor wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Type.h>
#include <clang/Basic/Module.h>
#include "CanonicalFilePathCache.h"
#include "CxxAstVisitorComponentContext.h"
#include "CxxLocationExtractor.h"
#include "NameHierarchy.h"
#include "ParserClient.h"
#include "utilityClang.h"
#endif

inline CxxIndexingContext::CxxIndexingContext(
	clang::ASTContext& astContext,
	ParserClient& client,
	CanonicalFilePathCache& canonicalFilePathCache,
	CxxLocationExtractor& locations)
	: m_astContext(astContext)
	, m_client(client)
	, m_canonicalFilePathCache(canonicalFilePathCache)
	, m_locations(locations)
	, m_symbols(client, canonicalFilePathCache)
{
}

inline void CxxIndexingContext::setContext(CxxAstVisitorComponentContext& context)
{
	m_context = &context;
}

// --- high-level idioms -----------------------------------------------------------------------

inline Id CxxIndexingContext::recordDeclaration(const clang::NamedDecl* d, SymbolKind kind)
{
	const Id symbolId = m_symbols.getOrCreateSymbolId(d);
	m_client.recordSymbolKind(symbolId, kind);
	m_client.recordLocation(symbolId, m_locations.getParseLocation(d->getLocation()), ParseLocationType::TOKEN);
	m_client.recordAccessKind(symbolId, utility::convertAccessSpecifier(d->getAccess()));
	m_client.recordDefinitionKind(symbolId, utility::getDefinitionKind(d));
	recordDeprecation(symbolId, d);
	return symbolId;
}

inline Id CxxIndexingContext::recordReference(
	const clang::NamedDecl* referenced, ReferenceKind kind, const clang::SourceLocation& location)
{
	const Id referencedSymbolId = m_symbols.getOrCreateSymbolId(referenced);
	const Id contextId = contextSymbolId();
	return m_client.recordReference(kind, referencedSymbolId, contextId, m_locations.getParseLocation(location));
}

// --- symbol identity -------------------------------------------------------------------------

inline Id CxxIndexingContext::getOrCreateSymbolId(const clang::NamedDecl* decl)
{
	return m_symbols.getOrCreateSymbolId(decl);
}

inline Id CxxIndexingContext::getOrCreateSymbolId(const clang::Type* type)
{
	return m_symbols.getOrCreateSymbolId(type);
}

inline Id CxxIndexingContext::getOrCreateSymbolId(CxxContext context)
{
	return m_symbols.getOrCreateSymbolId(context);
}

inline Id CxxIndexingContext::getOrCreateSymbolId(CxxContext context, const NameHierarchy& fallback)
{
	return m_symbols.getOrCreateSymbolId(context, fallback);
}

inline Id CxxIndexingContext::getOrCreateModuleSymbolId(const clang::Module* module)
{
	// Module names live in their own delimiter world (":"): the serialized name embeds the
	// delimiter, so module `foo` and namespace `foo` stay distinct nodes. A partition
	// ("primary:part") becomes a two-element hierarchy nesting under its primary module; dots
	// carry no semantic hierarchy in C++ module names and stay within one element.
	const std::string fullName = module->getFullModuleName();
	NameHierarchy name(NameDelimiterType::CXX_MODULE);

	const std::string::size_type colon = fullName.find(':');
	if (colon == std::string::npos)
	{
		name.push(fullName);
	}
	else
	{
		name.push(fullName.substr(0, colon));
		name.push(fullName.substr(colon + 1));

		const Id primaryId = m_client.recordSymbol(name.getRange(0, 1));
		m_client.recordSymbolKind(primaryId, SymbolKind::MODULE);
	}

	const Id symbolId = m_client.recordSymbol(name);
	m_client.recordSymbolKind(symbolId, SymbolKind::MODULE);
	return symbolId;
}

// --- current context -------------------------------------------------------------------------

inline CxxContext CxxIndexingContext::getContext(size_t skip) const
{
	return m_context->getContext(skip);
}

inline const clang::NamedDecl* CxxIndexingContext::getTopmostContextDecl(size_t skip) const
{
	return m_context->getTopmostContextDecl(skip);
}

inline Id CxxIndexingContext::contextSymbolId()
{
	return m_symbols.getOrCreateSymbolId(m_context->getContext());
}

// --- locations -------------------------------------------------------------------------------

inline ParseLocation CxxIndexingContext::getParseLocation(const clang::SourceLocation& loc) const
{
	return m_locations.getParseLocation(loc);
}

inline ParseLocation CxxIndexingContext::getParseLocation(const clang::SourceRange& range) const
{
	return m_locations.getParseLocation(range);
}

inline ParseLocation CxxIndexingContext::getParseLocationOfTagDeclBody(clang::TagDecl* decl) const
{
	return m_locations.getParseLocationOfTagDeclBody(decl);
}

inline ParseLocation CxxIndexingContext::getParseLocationOfFunctionBody(const clang::FunctionDecl* decl) const
{
	return m_locations.getParseLocationOfFunctionBody(decl);
}

inline ParseLocation CxxIndexingContext::getSignatureLocation(clang::FunctionDecl* decl) const
{
	return m_locations.getSignatureLocation(decl);
}

inline std::string CxxIndexingContext::getLocalSymbolName(const clang::SourceLocation& loc) const
{
	const ParseLocation location = m_locations.getParseLocation(loc);
	return m_canonicalFilePathCache.getCanonicalFilePath(location.fileId).fileName() + "<" +
		std::to_string(location.startLineNumber) + ":" + std::to_string(location.startColumnNumber) + ">";
}

// --- recording primitives --------------------------------------------------------------------

inline void CxxIndexingContext::recordSymbolKind(Id symbolId, SymbolKind symbolKind)
{
	m_client.recordSymbolKind(symbolId, symbolKind);
}

inline void CxxIndexingContext::recordAccessKind(Id symbolId, AccessKind accessKind)
{
	m_client.recordAccessKind(symbolId, accessKind);
}

inline void CxxIndexingContext::recordDefinitionKind(Id symbolId, DefinitionKind definitionKind)
{
	m_client.recordDefinitionKind(symbolId, definitionKind);
}

inline void CxxIndexingContext::recordNodeModifier(Id symbolId, NodeModifierMask modifier)
{
	m_client.recordNodeModifier(symbolId, modifier);
}

inline void CxxIndexingContext::recordNodeAttribute(Id symbolId, NodeAttributeKind key, const std::string& value)
{
	m_client.recordNodeAttribute(symbolId, key, value);
}

inline Id CxxIndexingContext::recordReference(
	ReferenceKind kind, Id referencedSymbolId, Id contextSymbolId, const ParseLocation& location)
{
	return m_client.recordReference(kind, referencedSymbolId, contextSymbolId, location);
}

inline void CxxIndexingContext::recordLocalSymbol(const std::string& name, const ParseLocation& location)
{
	m_client.recordLocalSymbol(name, location);
}

inline void CxxIndexingContext::recordLocation(Id elementId, const ParseLocation& location, ParseLocationType type)
{
	m_client.recordLocation(elementId, location, type);
}

// --- specialized recorders -------------------------------------------------------------------

inline void CxxIndexingContext::recordDeprecation(Id symbolId, const clang::Decl* d)
{
	if (const clang::DeprecatedAttr* attr = d->getAttr<clang::DeprecatedAttr>())
	{
		m_client.recordNodeModifier(symbolId, NODE_MODIFIER_DEPRECATED);
		// The bit carries the boolean; the message (if any) rides in a DEPRECATED
		// node_attribute -- same split the Swift/Rust producers use.
		const std::string message = attr->getMessage().str();
		if (!message.empty())
		{
			m_client.recordNodeAttribute(symbolId, NodeAttributeKind::DEPRECATED, message);
		}
	}
}

inline void CxxIndexingContext::recordExportStatus(Id symbolId, const clang::Decl* d)
{
	// A C++20 `export` is a per-declaration property, recorded where the declaration is indexed
	// rather than by traversing ExportDecl regions (see the module indexer component): the region
	// containing a header's declaration is only traversed by the one TU that CLAIMS the header,
	// and in the dual build that is usually a classic TU which never sees `export` at all.
	if (d->isInExportDeclContext())
	{
		m_client.recordNodeModifier(symbolId, NODE_MODIFIER_EXPORTED);
	}
}

inline void CxxIndexingContext::recordDeducedType(
	const clang::DeducedType* deducedType, Id contextSymbolId, const ParseLocation& keywordLocation)
{
	if (clang::QualType deduced = deducedType->getDeducedType(); !deduced.isNull())
	{
		recordDeducedQualType(deduced, contextSymbolId, keywordLocation);
	}
}

inline void CxxIndexingContext::recordDeducedQualType(
	clang::QualType deducedQualType, Id contextSymbolId, const ParseLocation& keywordLocation)
{
	// Record the deduced type location:
	const Id deducedTypeId = m_symbols.getOrCreateSymbolId(deducedQualType.getTypePtr());
	m_client.recordDefinitionKind(deducedTypeId, DefinitionKind::EXPLICIT);
	m_client.recordReference(ReferenceKind::TYPE_USAGE, deducedTypeId, contextSymbolId, keywordLocation);
}

inline void CxxIndexingContext::recordTemplateMemberSpecialization(
	const clang::MemberSpecializationInfo* memberSpecializationInfo,
	Id contextId,
	const ParseLocation& location,
	SymbolKind symbolKind)
{
	if (memberSpecializationInfo != nullptr)
	{
		const Id symbolId = m_symbols.getOrCreateSymbolId(memberSpecializationInfo->getInstantiatedFrom());
		m_client.recordSymbolKind(symbolId, symbolKind);
		m_client.recordReference(ReferenceKind::TEMPLATE_SPECIALIZATION, symbolId, contextId, location);
	}
}

inline CxxConceptReferenceRecorder CxxIndexingContext::concepts()
{
	return {m_client, m_symbols, m_locations, *m_context};
}

inline CxxDestructorCallRecorder CxxIndexingContext::destructorCalls()
{
	return {m_astContext, m_client, m_symbols, m_locations, *m_context};
}
