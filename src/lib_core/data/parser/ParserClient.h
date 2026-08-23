#ifndef PARSER_CLIENT_H
#define PARSER_CLIENT_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "AccessKind.h"
#include "DefinitionKind.h"
#include "NameHierarchy.h"
#include "NodeAttributeKind.h"
#include "NodeModifier.h"
#include "ParseLocation.h"
#include "ReferenceKind.h"
#include "SymbolKind.h"

#include <string>
#include <vector>
#endif

// TODO (petermost): Transfer documentation from https://github.com/CoatiSoftware/SourcetrailDB/blob/master/core/include/SourcetrailDBWriter.h

SRCTRL_EXPORT class ParserClient
{
public:
	virtual ~ParserClient() = default;

	virtual Id recordFile(const FilePath& filePath, bool indexed) = 0;
	virtual void recordFileLanguage(Id fileId, const std::string& languageIdentifier) = 0;

	virtual Id recordSymbol(const NameHierarchy& symbolName) = 0;
	virtual void recordSymbolKind(Id symbolId, SymbolKind symbolKind) = 0;
	virtual void recordAccessKind(Id symbolId, AccessKind accessKind) = 0;
	virtual void recordDefinitionKind(Id symbolId, DefinitionKind definitionKind) = 0;
	// Axis-2 capability bit(s) OR-ed into the node (e.g. NODE_MODIFIER_DEPRECATED).
	virtual void recordNodeModifier(Id symbolId, NodeModifierMask modifier) = 0;
	// Axis-3a sparse metadata: a display-only key->value fact on the node.
	virtual void recordNodeAttribute(Id symbolId, NodeAttributeKind key, const std::string& value) = 0;
	virtual Id recordReference(ReferenceKind referenceKind, Id referencedSymbolId, Id contextSymbolId, const ParseLocation& location) = 0;
	virtual void recordLocalSymbol(const std::string& name, const ParseLocation& location) = 0;
	virtual void recordLocation(Id elementId, const ParseLocation& location, ParseLocationType type) = 0;
	virtual void recordComment(const ParseLocation& location) = 0;

	virtual void recordError(const std::string& message, bool fatal, bool indexed, const FilePath& translationUnit, const ParseLocation& location) = 0;

	virtual bool hasContent() const = 0;

	//! The IPC channel-name prefixes this indexing run was configured with
	//! (project setting `channel_name_prefixes`, delivered on the IndexerCommand).
	//! It rides on the client because the client is the one per-run object every
	//! producer already holds -- the same reason the language mask does. Empty is
	//! the default and means the project declares no channels, so no producer
	//! records anything. See context/DESIGN_XLANG_BOUNDARIES.md.
	virtual const std::vector<std::string>& getChannelNamePrefixes() const
	{
		static const std::vector<std::string> none;
		return none;
	}
};

//! Whether a string literal names one of the project's declared IPC channels.
//!
//! Prefix, not glob, deliberately: this predicate is reimplemented by four
//! indexers in four languages, and "starts with" is the only spelling that
//! cannot drift between them -- which is exactly the tier-3 failure class
//! submodules/thoth-ipc/context/abi-consistency-review.md is about. An empty
//! prefix list matches nothing (the feature is off), and an empty prefix is
//! ignored rather than matching everything.
SRCTRL_EXPORT inline bool isChannelName(
	const std::string& literal, const std::vector<std::string>& prefixes)
{
	if (literal.empty())
	{
		return false;
	}
	for (const std::string& prefix: prefixes)
	{
		if (!prefix.empty() && literal.compare(0, prefix.size(), prefix) == 0)
		{
			return true;
		}
	}
	return false;
}

#endif	  // PARSER_CLIENT_H
