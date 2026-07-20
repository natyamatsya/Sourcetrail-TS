// Inline implementations for GarbageCollectorSerializer.h. Included at the end of that header
// (classic) or via the srctrl.interprocess wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include "garbage_collector_generated.h"
#endif

namespace IpcSerializer
{

inline flatbuffers::DetachedBuffer serializeGarbageCollector(const GarbageCollectorData& data)
{
	flatbuffers::FlatBufferBuilder builder(2048);

	std::vector<flatbuffers::Offset<Sourcetrail::Ipc::NameTimestamp>> fbInstances;
	for (const auto& [name, ts] : data.instances)
		fbInstances.push_back(Sourcetrail::Ipc::CreateNameTimestamp(
			builder, builder.CreateString(name), builder.CreateString(ts)));

	std::vector<flatbuffers::Offset<Sourcetrail::Ipc::NameTimestamp>> fbTimestamps;
	for (const auto& [name, ts] : data.memoryTimestamps)
		fbTimestamps.push_back(Sourcetrail::Ipc::CreateNameTimestamp(
			builder, builder.CreateString(name), builder.CreateString(ts)));

	auto fbState = Sourcetrail::Ipc::CreateGarbageCollectorState(
		builder, builder.CreateVector(fbInstances), builder.CreateVector(fbTimestamps));

	builder.Finish(fbState);
	return builder.Release();
}

inline GarbageCollectorData deserializeGarbageCollector(const uint8_t* buf, std::size_t /*len*/)
{
	GarbageCollectorData result;

	auto fb = Sourcetrail::Ipc::GetGarbageCollectorState(buf);
	if (!fb)
		return result;

	if (fb->instances())
		for (const auto* entry : *fb->instances())
			result.instances.emplace_back(
				entry->name() ? entry->name()->str() : "",
				entry->timestamp() ? entry->timestamp()->str() : "");

	if (fb->memory_timestamps())
		for (const auto* entry : *fb->memory_timestamps())
			result.memoryTimestamps.emplace_back(
				entry->name() ? entry->name()->str() : "",
				entry->timestamp() ? entry->timestamp()->str() : "");

	return result;
}

}
