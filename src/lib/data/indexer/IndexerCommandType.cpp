#include "IndexerCommandType.h"

std::string indexerCommandTypeToString(IndexerCommandType type)
{
	switch (type)
	{
	case INDEXER_COMMAND_CXX:
		return "indexer_command_cxx";
	case INDEXER_COMMAND_RUST:
		return "indexer_command_rust";
	case INDEXER_COMMAND_SWIFT:
		return "indexer_command_swift";
	case INDEXER_COMMAND_CUSTOM:
		return "indexer_command_custom";
	default:
		break;
	}
	return "indexer_command_unknown";
}

IndexerCommandType stringToIndexerCommandType(const std::string& s)
{
	if (s == indexerCommandTypeToString(INDEXER_COMMAND_CXX))
		return INDEXER_COMMAND_CXX;
	if (s == indexerCommandTypeToString(INDEXER_COMMAND_RUST))
		return INDEXER_COMMAND_RUST;
	if (s == indexerCommandTypeToString(INDEXER_COMMAND_SWIFT))
		return INDEXER_COMMAND_SWIFT;
	if (s == indexerCommandTypeToString(INDEXER_COMMAND_CUSTOM))
		return INDEXER_COMMAND_CUSTOM;
	return INDEXER_COMMAND_UNKNOWN;
}
