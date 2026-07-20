// Inline implementations for ParserClientImpl.h. Included at the end of that header (classic) or via
// the srctrl.indexer wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include "Edge.h"
#include "NodeKind.h"
#include "ParseLocation.h"
#endif

// ODR-safe home for the kind-mapping helpers (anonymous namespaces are an ODR trap in
// headers/inls).
namespace parser_client_impl_detail
{

inline NodeKind symbolKindToNodeKind(SymbolKind symbolKind) 
{
	switch (symbolKind)
	{
		case SymbolKind::ANNOTATION:
			return NODE_ANNOTATION;
		case SymbolKind::BUILTIN_TYPE:
			return NODE_BUILTIN_TYPE;
		case SymbolKind::CLASS:
			return NODE_CLASS;
		case SymbolKind::ENUM:
			return NODE_ENUM;
		case SymbolKind::ENUM_CONSTANT:
			return NODE_ENUM_CONSTANT;
		case SymbolKind::FIELD:
			return NODE_FIELD;
		case SymbolKind::FUNCTION:
			return NODE_FUNCTION;
		case SymbolKind::GLOBAL_VARIABLE:
			return NODE_GLOBAL_VARIABLE;
		case SymbolKind::INTERFACE:
			return NODE_INTERFACE;
		case SymbolKind::MACRO:
			return NODE_MACRO;
		case SymbolKind::METHOD:
			return NODE_METHOD;
		case SymbolKind::MODULE:
			return NODE_MODULE;
		case SymbolKind::NAMESPACE:
			return NODE_NAMESPACE;
		case SymbolKind::PACKAGE:
			return NODE_PACKAGE;
		case SymbolKind::STRUCT:
			return NODE_STRUCT;
		case SymbolKind::TYPEDEF:
			return NODE_TYPEDEF;
		case SymbolKind::TYPE_PARAMETER:
			return NODE_TYPE_PARAMETER;
		case SymbolKind::UNION:
			return NODE_UNION;
		case SymbolKind::RECORD:
			return NODE_RECORD;
		case SymbolKind::CONCEPT:
			return NODE_CONCEPT;
		default:
			break;
	}
	return NODE_SYMBOL;
}

inline Edge::EdgeType referenceKindToEdgeType(ReferenceKind referenceKind) 
{
	switch (referenceKind)
	{
		case ReferenceKind::TYPE_USAGE:
			return Edge::EDGE_TYPE_USAGE;
		case ReferenceKind::USAGE:
			return Edge::EDGE_USAGE;
		case ReferenceKind::CALL:
			return Edge::EDGE_CALL;
		case ReferenceKind::INHERITANCE:
			return Edge::EDGE_INHERITANCE;
		case ReferenceKind::OVERRIDE:
			return Edge::EDGE_OVERRIDE;
		case ReferenceKind::TYPE_ARGUMENT:
			return Edge::EDGE_TYPE_ARGUMENT;
		case ReferenceKind::TEMPLATE_SPECIALIZATION:
			return Edge::EDGE_TEMPLATE_SPECIALIZATION;
		case ReferenceKind::INCLUDE:
			return Edge::EDGE_INCLUDE;
		case ReferenceKind::IMPORT:
			return Edge::EDGE_IMPORT;
		case ReferenceKind::MACRO_USAGE:
			return Edge::EDGE_MACRO_USAGE;
		case ReferenceKind::ANNOTATION_USAGE:
			return Edge::EDGE_ANNOTATION_USAGE;
		default:
			break;
	}
	return Edge::EDGE_UNDEFINED;
}

inline LocationType parseLocationTypeToLocationType(ParseLocationType type) 
{
	switch (type)
	{
		case ParseLocationType::TOKEN:
			return LocationType::TOKEN;
		case ParseLocationType::SCOPE:
			return LocationType::SCOPE;
		case ParseLocationType::SIGNATURE:
			return LocationType::SIGNATURE;
		case ParseLocationType::QUALIFIER:
			return LocationType::QUALIFIER;
		case ParseLocationType::LOCAL:
			return LocationType::LOCAL_SYMBOL;
	}
	return LocationType::TOKEN;
}

}	 // namespace parser_client_impl_detail

inline ParserClientImpl::ParserClientImpl(std::shared_ptr<IntermediateStorage> storage): m_storage(storage) {}

inline Id ParserClientImpl::recordFile(const FilePath& filePath, bool indexed)
{
	Id fileId = addFileName(filePath);
	m_storage->addFile(StorageFile(fileId, filePath.str(), "", "", indexed, true));
	return fileId;
}

inline void ParserClientImpl::recordFileLanguage(Id fileId, const std::string& languageIdentifier)
{
	m_storage->setFileLanguage(fileId, languageIdentifier);
}

inline Id ParserClientImpl::recordSymbol(const NameHierarchy& symbolName)
{
	return addNodeHierarchy(symbolName);
}

inline void ParserClientImpl::recordSymbolKind(Id symbolId, SymbolKind symbolKind)
{
	m_storage->setNodeType(symbolId, parser_client_impl_detail::symbolKindToNodeKind(symbolKind));
}

inline void ParserClientImpl::recordAccessKind(Id symbolId, AccessKind accessKind)
{
	if (accessKind != AccessKind::NONE)
	{
		m_storage->addComponentAccess(StorageComponentAccess(symbolId, accessKind));
	}
}

inline void ParserClientImpl::recordDefinitionKind(Id symbolId, DefinitionKind definitionKind)
{
	if (definitionKind != DefinitionKind::NONE)
	{
		m_storage->addSymbol(StorageSymbol(symbolId, definitionKind));
	}
}

inline void ParserClientImpl::recordNodeModifier(Id symbolId, NodeModifierMask modifier)
{
	if (modifier != NODE_MODIFIER_NONE)
	{
		m_storage->addNodeModifier(symbolId, modifier);
	}
}

inline void ParserClientImpl::recordNodeAttribute(Id symbolId, NodeAttributeKind key, const std::string& value)
{
	if (key != NodeAttributeKind::NONE)
	{
		m_storage->addNodeAttribute(StorageNodeAttribute(symbolId, key, value));
	}
}

inline Id ParserClientImpl::recordReference(
	ReferenceKind referenceKind, Id referencedSymbolId, Id contextSymbolId, const ParseLocation& location)
{
	Id edgeId = addEdge(parser_client_impl_detail::referenceKindToEdgeType(referenceKind), contextSymbolId, referencedSymbolId);
	if (edgeId)
	{
		addSourceLocation(edgeId, location, LocationType::TOKEN);
	}
	return edgeId;
}

inline void ParserClientImpl::recordLocalSymbol(const std::string& name, const ParseLocation& location)
{
	const Id localSymbolId = m_storage->addLocalSymbol(name);
	addSourceLocation(localSymbolId, location, LocationType::LOCAL_SYMBOL);
}

inline void ParserClientImpl::recordLocation(Id elementId, const ParseLocation& location, ParseLocationType type)
{
	addSourceLocation(elementId, location, parser_client_impl_detail::parseLocationTypeToLocationType(type));
}

inline void ParserClientImpl::recordComment(const ParseLocation& location)
{
	if (!location.isValid())
	{
		return;
	}

	m_storage->addSourceLocation(StorageSourceLocationData(
		location.fileId,
		location.startLineNumber,
		location.startColumnNumber,
		location.endLineNumber,
		location.endColumnNumber,
		LocationType::COMMENT));
}

inline void ParserClientImpl::recordError(
	const std::string& message,
	bool fatal,
	bool indexed,
	const FilePath& translationUnit,
	const ParseLocation& location)
{
	if (location.fileId != 0)
	{
		Id errorId = m_storage->addError(
			StorageErrorData(message, translationUnit.str(), fatal, indexed));

		addSourceLocation(errorId, location, LocationType::ERROR);
	}
}

inline bool ParserClientImpl::hasContent() const
{
	return m_storage->getByteSize(1) > 0;
}


inline Id ParserClientImpl::addNodeHierarchy(const NameHierarchy& nameHierarchy)
{
	Id childNodeId = 0;
	Id firstNodeId = 0;
	for (size_t i = nameHierarchy.size(); i > 0; i--)
	{
		std::pair<Id, bool> ret = m_storage->addNode(StorageNodeData(NODE_SYMBOL, 
			NameHierarchy::serializeRange(nameHierarchy, 0, i)));

		if (!firstNodeId)
		{
			firstNodeId = ret.first;
		}

		if (childNodeId != 0)
		{
			addEdge(Edge::EDGE_MEMBER, ret.first, childNodeId);
		}

		if (!ret.second)
		{
			return firstNodeId;
		}

		childNodeId = ret.first;
	}
	return firstNodeId;
}

inline Id ParserClientImpl::addFileName(const FilePath& filePath)
{
	const std::string file = filePath.str();

	auto it = m_fileIdMap.find(file);
	if (it != m_fileIdMap.end())
	{
		return it->second;
	}

	const Id fileId = addNodeHierarchy(NameHierarchy(file, NameDelimiterType::FILE));
	m_storage->setNodeType(fileId, NODE_FILE);

	m_fileIdMap.emplace(file, fileId);
	return fileId;
}

inline Id ParserClientImpl::addEdge(Edge::EdgeType type, Id sourceId, Id targetId)
{
	if (sourceId && targetId)
	{
		return m_storage->addEdge(StorageEdgeData(type, sourceId, targetId));
	}
	return 0;
}

inline void ParserClientImpl::addSourceLocation(Id elementId, const ParseLocation& location, LocationType type)
{
	if (!location.isValid())
	{
		return;
	}

	Id sourceLocationId = m_storage->addSourceLocation(StorageSourceLocationData(
		location.fileId,
		location.startLineNumber,
		location.startColumnNumber,
		location.endLineNumber,
		location.endColumnNumber,
		type));

	m_storage->addOccurrence(StorageOccurrence(elementId, sourceLocationId));
}
