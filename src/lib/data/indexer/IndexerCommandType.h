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

// Transitional aliases to keep existing unscoped call sites compiling while
// migrating incrementally to explicit IndexerCommandType:: qualifiers.
constexpr auto INDEXER_COMMAND_UNKNOWN = IndexerCommandType::INDEXER_COMMAND_UNKNOWN;
constexpr auto INDEXER_COMMAND_CXX = IndexerCommandType::INDEXER_COMMAND_CXX;
constexpr auto INDEXER_COMMAND_RUST = IndexerCommandType::INDEXER_COMMAND_RUST;
constexpr auto INDEXER_COMMAND_SWIFT = IndexerCommandType::INDEXER_COMMAND_SWIFT;
constexpr auto INDEXER_COMMAND_CUSTOM = IndexerCommandType::INDEXER_COMMAND_CUSTOM;

std::string indexerCommandTypeToString(IndexerCommandType type);
IndexerCommandType stringToIndexerCommandType(const std::string& s);

#endif	  // INDEXER_COMMAND_TYPE_H
