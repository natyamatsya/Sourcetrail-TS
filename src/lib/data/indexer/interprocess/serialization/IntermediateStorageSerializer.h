#ifndef INTERMEDIATE_STORAGE_SERIALIZER_H
#define INTERMEDIATE_STORAGE_SERIALIZER_H

#include <cstdint>
#include <memory>

#include <flatbuffers/flatbuffers.h>

class IntermediateStorage;

namespace IpcSerializer
{

flatbuffers::DetachedBuffer serializeIntermediateStorage(const IntermediateStorage& storage);

std::shared_ptr<IntermediateStorage> deserializeIntermediateStorage(
	const uint8_t* buf, std::size_t len);

}

#endif // INTERMEDIATE_STORAGE_SERIALIZER_H
