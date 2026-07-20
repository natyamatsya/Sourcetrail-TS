#ifndef INDEXER_COMMAND_CODEC_H
#define INDEXER_COMMAND_CODEC_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <concepts>
#include <functional>
#include <memory>

#include "indexer_command_generated.h"

class IndexerCommand;
#endif

// Type-erased (de)serialization for one language's IndexerCommand. Registered per IndexerCommandType
// into IndexerCommandCodecRegistry so IndexerCommandSerializer dispatches by type without ever naming a
// concrete command class -- which is what keeps `lib` free of a link dependency on `lib_cxx` (and any
// future language package). No inheritance: a codec is a plain value holding two erased operations.
SRCTRL_EXPORT struct IndexerCommandCodec
{
	using SerializeFn = std::function<flatbuffers::Offset<Sourcetrail::Ipc::IndexerCommand>(
		flatbuffers::FlatBufferBuilder&, const IndexerCommand&)>;
	using DeserializeFn = std::function<std::shared_ptr<IndexerCommand>(
		const Sourcetrail::Ipc::IndexerCommand&)>;

	SerializeFn serialize;
	DeserializeFn deserialize;
};

// Compile-time contract a codec provider satisfies: any value type exposing these two operations can be
// erased into an IndexerCommandCodec -- no base class, no virtuals.
SRCTRL_EXPORT template <class T>
concept IndexerCommandCodecC = requires(
	const T& codec,
	flatbuffers::FlatBufferBuilder& builder,
	const IndexerCommand& command,
	const Sourcetrail::Ipc::IndexerCommand& fbCommand) {
	{
		codec.serialize(builder, command)
	} -> std::same_as<flatbuffers::Offset<Sourcetrail::Ipc::IndexerCommand>>;
	{ codec.deserialize(fbCommand) } -> std::same_as<std::shared_ptr<IndexerCommand>>;
};

// Erase a concept-satisfying provider value into the std::function-backed IndexerCommandCodec.
SRCTRL_EXPORT template <IndexerCommandCodecC Provider>
IndexerCommandCodec eraseIndexerCommandCodec(Provider provider)
{
	return IndexerCommandCodec{
		.serialize = [provider](flatbuffers::FlatBufferBuilder& builder, const IndexerCommand& command) {
			return provider.serialize(builder, command);
		},
		.deserialize = [provider](const Sourcetrail::Ipc::IndexerCommand& fbCommand) {
			return provider.deserialize(fbCommand);
		},
	};
}

#endif	  // INDEXER_COMMAND_CODEC_H
