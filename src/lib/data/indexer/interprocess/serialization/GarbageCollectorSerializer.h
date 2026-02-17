#ifndef GARBAGE_COLLECTOR_SERIALIZER_H
#define GARBAGE_COLLECTOR_SERIALIZER_H

#include <cstdint>
#include <string>
#include <vector>

#include <flatbuffers/flatbuffers.h>

namespace IpcSerializer
{

struct GarbageCollectorData
{
	std::vector<std::pair<std::string, std::string>> instances;
	std::vector<std::pair<std::string, std::string>> memoryTimestamps;
};

flatbuffers::DetachedBuffer serializeGarbageCollector(const GarbageCollectorData& data);
GarbageCollectorData deserializeGarbageCollector(const uint8_t* buf, std::size_t len);

}

#endif // GARBAGE_COLLECTOR_SERIALIZER_H
