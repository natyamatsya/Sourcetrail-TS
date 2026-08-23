#ifndef PARSER_CLIENT_IMPL_H
#define PARSER_CLIENT_IMPL_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "DefinitionKind.h"
#include "IntermediateStorage.h"
#include "LanguageMask.h"
#include "LocationType.h"
#include "ParserClient.h"

#include <memory>
#include <string>
#include <vector>
#endif

SRCTRL_EXPORT class ParserClientImpl: public ParserClient
{
public:
	// `languages` is the bit of the indexer driving this client; every node it
	// mints is stamped with it. Defaulted for tests and for the fixture-style
	// callers that record symbols without an indexer behind them.
	// `channelNamePrefixes` is the project's declared IPC channel-name prefixes,
	// which the producers read back through getChannelNamePrefixes().
	ParserClientImpl(
		std::shared_ptr<IntermediateStorage> storage,
		LanguageMask languages = LANGUAGE_NONE,
		std::vector<std::string> channelNamePrefixes = {});

	Id recordFile(const FilePath& filePath, bool indexed) override;
	void recordFileLanguage(Id fileId, const std::string& languageIdentifier) override;

	Id recordSymbol(const NameHierarchy& symbolName) override;
	void recordSymbolKind(Id symbolId, SymbolKind symbolKind) override;
	void recordAccessKind(Id symbolId, AccessKind accessKind) override;
	void recordDefinitionKind(Id symbolId, DefinitionKind definitionKind) override;
	void recordNodeModifier(Id symbolId, NodeModifierMask modifier) override;
	void recordNodeAttribute(Id symbolId, NodeAttributeKind key, const std::string& value) override;

	Id recordReference(
		ReferenceKind referenceKind,
		Id referencedSymbolId,
		Id contextSymbolId,
		const ParseLocation& location) override;

	void recordLocalSymbol(const std::string& name, const ParseLocation& location) override;
	void recordLocation(Id elementId, const ParseLocation& location, ParseLocationType type) override;
	void recordComment(const ParseLocation& location) override;

	void recordError(
		const std::string& message,
		bool fatal,
		bool indexed,
		const FilePath& translationUnit,
		const ParseLocation& location) override;

	bool hasContent() const override;

	const std::vector<std::string>& getChannelNamePrefixes() const override;

private:
	void addAccess(Id nodeId, AccessKind access);

	Id addNodeHierarchy(const NameHierarchy& nameHierarchy);
	Id addFileName(const FilePath& filePath);
	Id addEdge(Edge::EdgeType type, Id sourceId, Id targetId);

	void addSourceLocation(Id elementId, const ParseLocation& location, LocationType type);

	std::shared_ptr<IntermediateStorage> m_storage;
	LanguageMask m_languages;
	std::vector<std::string> m_channelNamePrefixes;
	std::map<std::string, Id> m_fileIdMap;
};


#ifndef SRCTRL_MODULE_PURVIEW
#include "ParserClientImpl.inl"
#endif

#endif	  // PARSER_CLIENT_IMPL_H
