// Inline implementations for IndexingStatusSerializer.h. Included at the end of that header
// (classic) or via the srctrl.interprocess wrapper (purview); not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include "indexing_status_generated.h"
#include "logging.h"
#endif

namespace IpcSerializer
{

inline flatbuffers::DetachedBuffer serializeIndexingStatus(const IndexingStatusData& status)
{
	flatbuffers::FlatBufferBuilder builder(4096);

	// Every vector field is omitted (null offset) when empty, matching the Rust
	// writer (ipc/status.rs) so the three frontends agree on the empty-status wire
	// form. This is required for finished_process_ids: an empty [uint64] built via
	// CreateVector is only 4-aligned, which the strict flatbuffers Swift verifier
	// rejects as a mis-aligned uint64 (8-byte element) — it made every app
	// IndexingStatus unreadable by the Swift indexer. A null field reads back as an
	// empty vector everywhere. The offset-vector fields are 4-aligned and would
	// verify either way, but are nulled too for consistency.
	flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>>
		fbIndexing = 0;
	if (!status.indexingFilePaths.empty())
	{
		std::vector<flatbuffers::Offset<flatbuffers::String>> paths;
		for (const auto& p : status.indexingFilePaths)
			paths.push_back(builder.CreateString(p));
		fbIndexing = builder.CreateVector(paths);
	}

	flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<Sourcetrail::Ipc::ProcessFile>>>
		fbCurrent = 0;
	if (!status.currentFiles.empty())
	{
		std::vector<flatbuffers::Offset<Sourcetrail::Ipc::ProcessFile>> current;
		for (const auto& [pid, path] : status.currentFiles)
			current.push_back(Sourcetrail::Ipc::CreateProcessFile(
				builder, static_cast<uint64_t>(pid), builder.CreateString(path)));
		fbCurrent = builder.CreateVector(current);
	}

	flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>>
		fbCrashed = 0;
	if (!status.crashedFilePaths.empty())
	{
		std::vector<flatbuffers::Offset<flatbuffers::String>> crashed;
		for (const auto& p : status.crashedFilePaths)
			crashed.push_back(builder.CreateString(p));
		fbCrashed = builder.CreateVector(crashed);
	}

	flatbuffers::Offset<flatbuffers::Vector<uint64_t>> fbFinished = 0;
	if (!status.finishedProcessIds.empty())
		fbFinished = builder.CreateVector(
			reinterpret_cast<const uint64_t*>(status.finishedProcessIds.data()),
			status.finishedProcessIds.size());

	auto fbStatus = Sourcetrail::Ipc::CreateIndexingStatus(
		builder,
		fbIndexing,
		fbCurrent,
		fbCrashed,
		fbFinished,
		status.indexingInterrupted,
		status.queueStopped);

	builder.Finish(fbStatus);
	return builder.Release();
}

inline IndexingStatusData deserializeIndexingStatus(const uint8_t* buf, std::size_t len)
{
	IndexingStatusData result;

	// Verify before reading rather than trusting unverified bytes (ADR-0003). The
	// caller (readStatus) has already short-circuited the zeroed/empty segment, so
	// an unverifiable buffer here is a genuine fault, not "no status yet" — surface
	// it (log) and degrade to an empty status instead of reading garbage silently.
	flatbuffers::Verifier verifier(buf, len);
	if (!Sourcetrail::Ipc::VerifyIndexingStatusBuffer(verifier))
	{
		LOG_ERROR("IndexingStatus failed FlatBuffers verification; treating as empty status");
		return result;
	}

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
	result.queueStopped = fb->queue_stopped();

	return result;
}

}
