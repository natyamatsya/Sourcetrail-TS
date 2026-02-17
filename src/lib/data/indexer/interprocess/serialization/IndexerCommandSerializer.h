#ifndef INDEXER_COMMAND_SERIALIZER_H
#define INDEXER_COMMAND_SERIALIZER_H

#include <cstdint>
#include <memory>
#include <vector>

#include <flatbuffers/flatbuffers.h>

class IndexerCommand;

namespace IpcSerializer
{

flatbuffers::DetachedBuffer serializeIndexerCommands(
	const std::vector<std::shared_ptr<IndexerCommand>>& commands);

std::vector<std::shared_ptr<IndexerCommand>> deserializeIndexerCommands(
	const uint8_t* buf, std::size_t len);

}

#endif // INDEXER_COMMAND_SERIALIZER_H
