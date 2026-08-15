#ifndef INDEXER_COMMAND_TYPE_H
#define INDEXER_COMMAND_TYPE_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "LanguageMask.h"
#include "language_package_flags.h"

#include <string>
#endif

SRCTRL_EXPORT enum class IndexerCommandType
{
	INDEXER_COMMAND_UNKNOWN,
	INDEXER_COMMAND_CXX,
	INDEXER_COMMAND_RUST,
	INDEXER_COMMAND_SWIFT,
	INDEXER_COMMAND_ZIG,
	INDEXER_COMMAND_CUSTOM
};


SRCTRL_EXPORT std::string indexerCommandTypeToString(IndexerCommandType type);
SRCTRL_EXPORT IndexerCommandType stringToIndexerCommandType(const std::string& s);

// The language bit an indexer stamps on every node it records. CUSTOM and
// UNKNOWN get no bit: a custom command runs an arbitrary tool, and claiming a
// language for it would be a guess written into storage.
SRCTRL_EXPORT LanguageMask languageMaskForIndexerCommandType(IndexerCommandType type);


#ifndef SRCTRL_MODULE_PURVIEW
#include "IndexerCommandType.inl"
#endif

#endif	  // INDEXER_COMMAND_TYPE_H
