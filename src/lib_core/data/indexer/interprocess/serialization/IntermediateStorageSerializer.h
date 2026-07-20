#ifndef INTERMEDIATE_STORAGE_SERIALIZER_H
#define INTERMEDIATE_STORAGE_SERIALIZER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <cstdint>
#include <memory>

#include <flatbuffers/flatbuffers.h>

class IntermediateStorage;
#endif

namespace IpcSerializer
{

SRCTRL_EXPORT flatbuffers::DetachedBuffer serializeIntermediateStorage(const IntermediateStorage& storage);

SRCTRL_EXPORT std::shared_ptr<IntermediateStorage> deserializeIntermediateStorage(
	const uint8_t* buf, std::size_t len);

}

#ifndef SRCTRL_MODULE_PURVIEW
#include "IntermediateStorageSerializer.inl"
#endif

#endif // INTERMEDIATE_STORAGE_SERIALIZER_H
