#ifndef GARBAGE_COLLECTOR_SERIALIZER_H
#define GARBAGE_COLLECTOR_SERIALIZER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <cstdint>
#include <string>
#include <vector>

#include <flatbuffers/flatbuffers.h>
#endif

namespace IpcSerializer
{

SRCTRL_EXPORT struct GarbageCollectorData
{
	std::vector<std::pair<std::string, std::string>> instances;
	std::vector<std::pair<std::string, std::string>> memoryTimestamps;
};

SRCTRL_EXPORT flatbuffers::DetachedBuffer serializeGarbageCollector(const GarbageCollectorData& data);
SRCTRL_EXPORT GarbageCollectorData deserializeGarbageCollector(const uint8_t* buf, std::size_t len);

}

#ifndef SRCTRL_MODULE_PURVIEW
#include "GarbageCollectorSerializer.inl"
#endif

#endif // GARBAGE_COLLECTOR_SERIALIZER_H
