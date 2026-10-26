#ifndef INDEXING_RUN_STATE_H
#define INDEXING_RUN_STATE_H

#include <atomic>

#include "Condition.h"

// What the steps of an indexing run share as it goes: the counts the dialogs and the
// report show, and when the stages began and ended, which the streams that follow
// them wait for. Set by one step, read by others, from their own threads.
struct IndexingRunState
{
	std::atomic<int> sourceFileCount = 0;	// to index (less the duplicates skipped)
	std::atomic<int> indexedSourceFileCount = 0;
	std::atomic<bool> interrupted = false;
	std::atomic<double> clearSeconds = 0;	// clearing the files to re-index
	std::atomic<double> indexSeconds = 0;	// indexing them

	// The command queue has its first commands: the build starts its indexers ...
	std::atomic<bool> commandQueueStarted = false;
	// ... and gets no more.
	std::atomic<bool> commandQueueStopped = false;
	// The indexers run: merging and writing their storages starts.
	std::atomic<bool> indexersStarted = false;
	// ... and ended.
	std::atomic<bool> indexersStopped = false;

	// Notified when one of the stages above began or ended.
	execution::Condition stagesChanged;

	void addIndexSeconds(double seconds)
	{
		indexSeconds.fetch_add(seconds);
	}
	void begin(std::atomic<bool>& stage)
	{
		stage = true;
		stagesChanged.notify();
	}
	auto begun(const std::atomic<bool>& stage)
	{
		return stagesChanged.until([&stage]() { return stage.load(); });
	}
};

#endif	  // INDEXING_RUN_STATE_H
