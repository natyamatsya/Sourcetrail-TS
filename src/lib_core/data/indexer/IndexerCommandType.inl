// Inline implementations for IndexerCommandType.h. Included at the end of that header (classic) or via
// the srctrl.indexer wrapper (purview); not a standalone TU.

#pragma once

inline std::string indexerCommandTypeToString(IndexerCommandType type)
{
	switch (type)
	{
	case IndexerCommandType::INDEXER_COMMAND_CXX:
		return "indexer_command_cxx";
	case IndexerCommandType::INDEXER_COMMAND_RUST:
		return "indexer_command_rust";
	case IndexerCommandType::INDEXER_COMMAND_SWIFT:
		return "indexer_command_swift";
	case IndexerCommandType::INDEXER_COMMAND_ZIG:
		return "indexer_command_zig";
	case IndexerCommandType::INDEXER_COMMAND_CUSTOM:
		return "indexer_command_custom";
	default:
		break;
	}
	return "indexer_command_unknown";
}

inline IndexerCommandType stringToIndexerCommandType(const std::string& s)
{
	if (s == indexerCommandTypeToString(IndexerCommandType::INDEXER_COMMAND_CXX))
		return IndexerCommandType::INDEXER_COMMAND_CXX;
	if (s == indexerCommandTypeToString(IndexerCommandType::INDEXER_COMMAND_RUST))
		return IndexerCommandType::INDEXER_COMMAND_RUST;
	if (s == indexerCommandTypeToString(IndexerCommandType::INDEXER_COMMAND_SWIFT))
		return IndexerCommandType::INDEXER_COMMAND_SWIFT;
	if (s == indexerCommandTypeToString(IndexerCommandType::INDEXER_COMMAND_ZIG))
		return IndexerCommandType::INDEXER_COMMAND_ZIG;
	if (s == indexerCommandTypeToString(IndexerCommandType::INDEXER_COMMAND_CUSTOM))
		return IndexerCommandType::INDEXER_COMMAND_CUSTOM;
	return IndexerCommandType::INDEXER_COMMAND_UNKNOWN;
}
