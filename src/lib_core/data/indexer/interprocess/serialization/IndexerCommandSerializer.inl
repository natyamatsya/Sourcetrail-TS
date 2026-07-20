// Inline implementations for IndexerCommandSerializer.h. Included at the end of that header
// (classic) or via the srctrl.interprocess wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>

#include "IndexerCommand.h"
#include "IndexerCommandCodecRegistry.h"
#include "IndexerCommandType.h"
#include "logging.h"

#include "indexer_command_generated.h"
#endif

// ODR-safe home for the wire-type mapping (anonymous namespaces are an ODR trap in headers/inls).
namespace indexer_command_serializer_detail
{
// Map the wire-format discriminator back to the native IndexerCommandType the registry is keyed by.
inline IndexerCommandType toNativeType(Sourcetrail::Ipc::IndexerCommandType type)
{
	using enum IndexerCommandType;
	switch (type)
	{
	case Sourcetrail::Ipc::IndexerCommandType_Cxx:
		return INDEXER_COMMAND_CXX;
	case Sourcetrail::Ipc::IndexerCommandType_Rust:
		return INDEXER_COMMAND_RUST;
	case Sourcetrail::Ipc::IndexerCommandType_Swift:
		return INDEXER_COMMAND_SWIFT;
	case Sourcetrail::Ipc::IndexerCommandType_Zig:
		return INDEXER_COMMAND_ZIG;
	default:
		return INDEXER_COMMAND_UNKNOWN;
	}
}
}	 // namespace indexer_command_serializer_detail

namespace IpcSerializer
{

inline flatbuffers::DetachedBuffer serializeIndexerCommands(
	const std::vector<std::shared_ptr<IndexerCommand>>& commands)
{
	flatbuffers::FlatBufferBuilder builder(4096);

	std::vector<flatbuffers::Offset<Sourcetrail::Ipc::IndexerCommand>> fbCommands;
	fbCommands.reserve(commands.size());

	const IndexerCommandCodecRegistry& registry = IndexerCommandCodecRegistry::getInstance();

	for (const auto& cmd: commands)
	{
		const IndexerCommandCodec* codec = registry.find(cmd->getIndexerCommandType());
		if (codec == nullptr)
		{
			LOG_ERROR(
				"Cannot serialize IndexerCommand for file: " + cmd->getSourceFilePath().str() +
				". No codec registered for its type.");
			continue;
		}
		fbCommands.push_back(codec->serialize(builder, *cmd));
	}

	auto queue = Sourcetrail::Ipc::CreateIndexerCommandQueue(
		builder, builder.CreateVector(fbCommands));
	builder.Finish(queue);

	return builder.Release();
}

inline std::vector<std::shared_ptr<IndexerCommand>> deserializeIndexerCommands(
	const uint8_t* buf, std::size_t /*len*/)
{
	std::vector<std::shared_ptr<IndexerCommand>> result;

	auto queue = Sourcetrail::Ipc::GetIndexerCommandQueue(buf);
	if (!queue || !queue->commands())
		return result;

	const IndexerCommandCodecRegistry& registry = IndexerCommandCodecRegistry::getInstance();

	for (const auto* fbCmd: *queue->commands())
	{
		if (!fbCmd)
			continue;

		const IndexerCommandCodec* codec = registry.find(
			indexer_command_serializer_detail::toNativeType(fbCmd->type()));
		if (codec == nullptr)
		{
			LOG_ERROR(
				"Cannot deserialize IndexerCommand for file: " +
				std::string(fbCmd->source_file_path() ? fbCmd->source_file_path()->c_str() : "<null>") +
				". Unknown type.");
			continue;
		}

		std::shared_ptr<IndexerCommand> command = codec->deserialize(*fbCmd);
		if (command)
		{
			// Restore the source-group tag the codec left untouched (common to every command type).
			if (fbCmd->source_group_id())
				command->setSourceGroupId(fbCmd->source_group_id()->str());
			result.push_back(std::move(command));
		}
	}

	return result;
}

}
