#ifndef INDEXER_COMMAND_TYPE_H
#define INDEXER_COMMAND_TYPE_H

#include "language_package_flags.h"

#include <string>

enum class IndexerCommandType
{
	INDEXER_COMMAND_UNKNOWN,
	INDEXER_COMMAND_CXX,
	INDEXER_COMMAND_RUST,
	INDEXER_COMMAND_SWIFT,
	INDEXER_COMMAND_CUSTOM
};


std::string indexerCommandTypeToString(IndexerCommandType type);
IndexerCommandType stringToIndexerCommandType(const std::string& s);

#endif	  // INDEXER_COMMAND_TYPE_H
