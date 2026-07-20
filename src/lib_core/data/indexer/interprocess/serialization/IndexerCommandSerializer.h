#ifndef INDEXER_COMMAND_SERIALIZER_H
#define INDEXER_COMMAND_SERIALIZER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <cstdint>
#include <memory>
#include <vector>

#include <flatbuffers/flatbuffers.h>

class IndexerCommand;
#endif

namespace IpcSerializer
{

SRCTRL_EXPORT flatbuffers::DetachedBuffer serializeIndexerCommands(
	const std::vector<std::shared_ptr<IndexerCommand>>& commands);

SRCTRL_EXPORT std::vector<std::shared_ptr<IndexerCommand>> deserializeIndexerCommands(
	const uint8_t* buf, std::size_t len);

}

#ifndef SRCTRL_MODULE_PURVIEW
#include "IndexerCommandSerializer.inl"
#endif

#endif // INDEXER_COMMAND_SERIALIZER_H
