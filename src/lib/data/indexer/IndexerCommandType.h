#ifndef INDEXER_COMMAND_TYPE_H
#define INDEXER_COMMAND_TYPE_H

#include "language_package_flags.h"

#include <string>

enum IndexerCommandType
{
	INDEXER_COMMAND_UNKNOWN,
#if BUILD_CXX_LANGUAGE_PACKAGE
	INDEXER_COMMAND_CXX,
#endif	  // BUILD_CXX_LANGUAGE_PACKAGE
	INDEXER_COMMAND_RUST,
	INDEXER_COMMAND_SWIFT,
	INDEXER_COMMAND_CUSTOM
};

std::string indexerCommandTypeToString(IndexerCommandType type);
IndexerCommandType stringToIndexerCommandType(const std::string& s);

#endif	  // INDEXER_COMMAND_TYPE_H
