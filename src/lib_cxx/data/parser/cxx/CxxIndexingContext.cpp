#include "CxxIndexingContext.h"

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

CxxIndexingContext::CxxIndexingContext(
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

void CxxIndexingContext::setContext(CxxAstVisitorComponentContext& context)
{
	m_context = &context;
}

// --- high-level idioms -----------------------------------------------------------------------

Id CxxIndexingContext::recordDeclaration(const clang::NamedDecl* d, SymbolKind kind)
{
	const Id symbolId = m_symbols.getOrCreateSymbolId(d);
	m_client.recordSymbolKind(symbolId, kind);
	m_client.recordLocation(symbolId, m_locations.getParseLocation(d->getLocation()), ParseLocationType::TOKEN);
	m_client.recordAccessKind(symbolId, utility::convertAccessSpecifier(d->getAccess()));
	m_client.recordDefinitionKind(symbolId, utility::getDefinitionKind(d));
	recordDeprecation(symbolId, d);
	return symbolId;
}

Id CxxIndexingContext::recordReference(
	const clang::NamedDecl* referenced, ReferenceKind kind, const clang::SourceLocation& location)
{
	const Id referencedSymbolId = m_symbols.getOrCreateSymbolId(referenced);
	const Id contextId = contextSymbolId();
	return m_client.recordReference(kind, referencedSymbolId, contextId, m_locations.getParseLocation(location));
}

// --- symbol identity -------------------------------------------------------------------------

Id CxxIndexingContext::getOrCreateSymbolId(const clang::NamedDecl* decl)
{
	return m_symbols.getOrCreateSymbolId(decl);
}

Id CxxIndexingContext::getOrCreateSymbolId(const clang::Type* type)
{
	return m_symbols.getOrCreateSymbolId(type);
}

Id CxxIndexingContext::getOrCreateSymbolId(CxxContext context)
{
	return m_symbols.getOrCreateSymbolId(context);
}

Id CxxIndexingContext::getOrCreateSymbolId(CxxContext context, const NameHierarchy& fallback)
{
	return m_symbols.getOrCreateSymbolId(context, fallback);
}

Id CxxIndexingContext::getOrCreateModuleSymbolId(const clang::Module* module)
{
	NameHierarchy name(module->getFullModuleName(), NameDelimiterType::CXX);
	const Id symbolId = m_client.recordSymbol(name);
	m_client.recordSymbolKind(symbolId, SymbolKind::MODULE);
	return symbolId;
}

// --- current context -------------------------------------------------------------------------

CxxContext CxxIndexingContext::getContext(size_t skip) const
{
	return m_context->getContext(skip);
}

const clang::NamedDecl* CxxIndexingContext::getTopmostContextDecl(size_t skip) const
{
	return m_context->getTopmostContextDecl(skip);
}

Id CxxIndexingContext::contextSymbolId()
{
	return m_symbols.getOrCreateSymbolId(m_context->getContext());
}

// --- locations -------------------------------------------------------------------------------

ParseLocation CxxIndexingContext::getParseLocation(const clang::SourceLocation& loc) const
{
	return m_locations.getParseLocation(loc);
}

ParseLocation CxxIndexingContext::getParseLocation(const clang::SourceRange& range) const
{
	return m_locations.getParseLocation(range);
}

ParseLocation CxxIndexingContext::getParseLocationOfTagDeclBody(clang::TagDecl* decl) const
{
	return m_locations.getParseLocationOfTagDeclBody(decl);
}

ParseLocation CxxIndexingContext::getParseLocationOfFunctionBody(const clang::FunctionDecl* decl) const
{
	return m_locations.getParseLocationOfFunctionBody(decl);
}

ParseLocation CxxIndexingContext::getSignatureLocation(clang::FunctionDecl* decl) const
{
	return m_locations.getSignatureLocation(decl);
}

std::string CxxIndexingContext::getLocalSymbolName(const clang::SourceLocation& loc) const
{
	const ParseLocation location = m_locations.getParseLocation(loc);
	return m_canonicalFilePathCache.getCanonicalFilePath(location.fileId).fileName() + "<" +
		std::to_string(location.startLineNumber) + ":" + std::to_string(location.startColumnNumber) + ">";
}

// --- recording primitives --------------------------------------------------------------------

void CxxIndexingContext::recordSymbolKind(Id symbolId, SymbolKind symbolKind)
{
	m_client.recordSymbolKind(symbolId, symbolKind);
}

void CxxIndexingContext::recordAccessKind(Id symbolId, AccessKind accessKind)
{
	m_client.recordAccessKind(symbolId, accessKind);
}

void CxxIndexingContext::recordDefinitionKind(Id symbolId, DefinitionKind definitionKind)
{
	m_client.recordDefinitionKind(symbolId, definitionKind);
}

void CxxIndexingContext::recordNodeModifier(Id symbolId, NodeModifierMask modifier)
{
	m_client.recordNodeModifier(symbolId, modifier);
}

void CxxIndexingContext::recordNodeAttribute(Id symbolId, NodeAttributeKind key, const std::string& value)
{
	m_client.recordNodeAttribute(symbolId, key, value);
}

Id CxxIndexingContext::recordReference(
	ReferenceKind kind, Id referencedSymbolId, Id contextSymbolId, const ParseLocation& location)
{
	return m_client.recordReference(kind, referencedSymbolId, contextSymbolId, location);
}

void CxxIndexingContext::recordLocalSymbol(const std::string& name, const ParseLocation& location)
{
	m_client.recordLocalSymbol(name, location);
}

void CxxIndexingContext::recordLocation(Id elementId, const ParseLocation& location, ParseLocationType type)
{
	m_client.recordLocation(elementId, location, type);
}

// --- specialized recorders -------------------------------------------------------------------

void CxxIndexingContext::recordDeprecation(Id symbolId, const clang::Decl* d)
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

void CxxIndexingContext::recordDeducedType(
	const clang::DeducedType* deducedType, Id contextSymbolId, const ParseLocation& keywordLocation)
{
	if (clang::QualType deduced = deducedType->getDeducedType(); !deduced.isNull())
	{
		recordDeducedQualType(deduced, contextSymbolId, keywordLocation);
	}
}

void CxxIndexingContext::recordDeducedQualType(
	clang::QualType deducedQualType, Id contextSymbolId, const ParseLocation& keywordLocation)
{
	// Record the deduced type location:
	const Id deducedTypeId = m_symbols.getOrCreateSymbolId(deducedQualType.getTypePtr());
	m_client.recordDefinitionKind(deducedTypeId, DefinitionKind::EXPLICIT);
	m_client.recordReference(ReferenceKind::TYPE_USAGE, deducedTypeId, contextSymbolId, keywordLocation);
}

void CxxIndexingContext::recordTemplateMemberSpecialization(
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

CxxConceptReferenceRecorder CxxIndexingContext::concepts()
{
	return {m_client, m_symbols, m_locations, *m_context};
}

CxxDestructorCallRecorder CxxIndexingContext::destructorCalls()
{
	return {m_astContext, m_client, m_symbols, m_locations, *m_context};
}
