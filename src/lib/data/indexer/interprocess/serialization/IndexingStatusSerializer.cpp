#include "IndexingStatusSerializer.h"

#include "indexing_status_generated.h"

namespace IpcSerializer
{

flatbuffers::DetachedBuffer serializeIndexingStatus(const IndexingStatusData& status)
{
	flatbuffers::FlatBufferBuilder builder(4096);

	std::vector<flatbuffers::Offset<flatbuffers::String>> fbIndexing;
	for (const auto& p : status.indexingFilePaths)
		fbIndexing.push_back(builder.CreateString(p));

	std::vector<flatbuffers::Offset<Sourcetrail::Ipc::ProcessFile>> fbCurrent;
	for (const auto& [pid, path] : status.currentFiles)
		fbCurrent.push_back(Sourcetrail::Ipc::CreateProcessFile(
			builder, static_cast<uint64_t>(pid), builder.CreateString(path)));

	std::vector<flatbuffers::Offset<flatbuffers::String>> fbCrashed;
	for (const auto& p : status.crashedFilePaths)
		fbCrashed.push_back(builder.CreateString(p));

	auto fbFinished = builder.CreateVector(
		reinterpret_cast<const uint64_t*>(status.finishedProcessIds.data()),
		status.finishedProcessIds.size());

	auto fbStatus = Sourcetrail::Ipc::CreateIndexingStatus(
		builder,
		builder.CreateVector(fbIndexing),
		builder.CreateVector(fbCurrent),
		builder.CreateVector(fbCrashed),
		fbFinished,
		status.indexingInterrupted);

	builder.Finish(fbStatus);
	return builder.Release();
}

IndexingStatusData deserializeIndexingStatus(const uint8_t* buf, std::size_t /*len*/)
{
	IndexingStatusData result;

	auto fb = Sourcetrail::Ipc::GetIndexingStatus(buf);
	if (!fb)
		return result;

	if (fb->indexing_file_paths())
		for (const auto* p : *fb->indexing_file_paths())
			result.indexingFilePaths.push_back(p->str());

	if (fb->current_files())
		for (const auto* cf : *fb->current_files())
			result.currentFiles.emplace_back(
				static_cast<std::size_t>(cf->process_id()),
				cf->file_path() ? cf->file_path()->str() : "");

	if (fb->crashed_file_paths())
		for (const auto* p : *fb->crashed_file_paths())
			result.crashedFilePaths.push_back(p->str());

	if (fb->finished_process_ids())
		for (auto id : *fb->finished_process_ids())
			result.finishedProcessIds.push_back(static_cast<std::size_t>(id));

	result.indexingInterrupted = fb->indexing_interrupted();

	return result;
}

}
