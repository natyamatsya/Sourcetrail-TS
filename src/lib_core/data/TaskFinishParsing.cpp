#include "TaskFinishParsing.h"

#include "DialogView.h"
#include "IndexingRunState.h"
#ifndef SRCTRL_MODULE_BUILD
#include "MessageIndexingFinished.h"
#include "MessageIndexingStatus.h"
#include "MessageStatus.h"
#endif
#include "PersistentStorage.h"
#include "ScopedFunctor.h"
#ifndef SRCTRL_MODULE_BUILD
#include "TimeStamp.h"
#endif
#include "logging.h"

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.messaging;
import srctrl.utility;
#endif

TaskFinishParsing::TaskFinishParsing(
	std::shared_ptr<PersistentStorage> storage, std::shared_ptr<DialogView> dialogView)
	: m_storage(storage), m_dialogView(dialogView)
{
}

void TaskFinishParsing::abandoned()
{
	m_dialogView->clearDialogs();

	MessageStatus("An unknown exception was thrown during indexing.", true, false).dispatch();
	// The terminal MessageIndexingFinished of a terminated run comes from the pipeline's owner
	// (Project::startIndexing, as the run completes stopped) -- dispatching it here too would
	// double the terminal event.
}

exec::task<DatabasePolicy> TaskFinishParsing::run(const IndexingRunState& state)
{
	// Ended before the decision (stopped, or a throw): the coroutine unwinds through here.
	bool decided = false;
	[[maybe_unused]] ScopedFunctor unwound([this, &decided]() {
		if (!decided)
		{
			abandoned();
		}
	});

#ifdef SOURCETRAIL_TURSO_CONCURRENT
	// Drain the concurrent Turso writers before any read of the finished database.
	m_storage->finishConcurrentTurso();
#endif
	m_storage->setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);

	report(state);

	// Waits for the decision, holding no thread.
	co_await m_decision->decided.until([decision = m_decision]() { return decision->policy >= 0; });
	decided = true;
	MessageIndexingStatus(false).dispatch();
	m_storage.reset();
	co_return static_cast<DatabasePolicy>(m_decision->policy.load());
}

void TaskFinishParsing::report(const IndexingRunState& state)
{
	TimeStamp start = TimeStamp::now();

	m_dialogView->showUnknownProgressDialog("Finish Indexing", "Optimizing database");
	m_storage->setBulkWritePragmas(false);	  // restore SYNCHRONOUS=NORMAL before the DB goes live
	m_storage->optimizeMemory();
	m_dialogView->hideUnknownProgressDialog();

	const double time = TimeStamp::durationSeconds(start) + state.clearSeconds + state.indexSeconds;
	const int indexedSourceFileCount = state.indexedSourceFileCount;
	const int sourceFileCount = state.sourceFileCount;
	const bool interruptedIndexing = state.interrupted;

	ErrorCountInfo errorInfo = m_storage->getErrorCount();

	std::string status;
	status += interruptedIndexing ? "Interrupted indexing: " : "Finished indexing: ";
	status += std::to_string(indexedSourceFileCount) + "/" + std::to_string(sourceFileCount) +
		" source files indexed; ";
	status += TimeStamp::secondsToString(time);
	status += "; " + std::to_string(errorInfo.total) + " error" +
		(errorInfo.total != 1 ? "s" : "");
	if (errorInfo.fatal > 0)
	{
		status += " (" + std::to_string(errorInfo.fatal) + " fatal)";
	}
	MessageStatus(status, false, false).dispatch();

	StorageStats stats = m_storage->getStorageStats();

	// Indexing health signal: a run can "finish" while silently dropping whole
	// translation units to fatal errors (e.g. a misconfigured toolchain/sysroot,
	// or a source group with no indexed paths). The success-styled status above
	// hides that, so emit a distinct warning when the index is degraded -- with a
	// concrete count of fatal errors and files left incomplete.
	const int incompleteFileCount = static_cast<int>(stats.fileCount) -
		static_cast<int>(stats.completedFileCount);
	if (errorInfo.fatal > 0)
	{
		LOG_WARNING(
			"Indexing health: " + std::to_string(errorInfo.fatal) +
			" fatal error(s); " + std::to_string(incompleteFileCount) +
			" file(s) left incomplete. Parts of the code base are likely missing from "
			"the index -- verify the compile flags and toolchain setup (on macOS, an "
			"unresolved SDK sysroot is a common cause).");
	}

#ifdef SOURCETRAIL_TURSO_CONCURRENT
	// Degraded fan-out run (S5): the sole-writer path lost batches after retry
	// exhaustion, or the Turso->SQLite export failed — data is missing from the
	// index even though the run "finished". Re-index with the fan-out disabled
	// ("indexing/multi_group_fan_out": "off") to recover serially.
	if (m_storage->concurrentTursoLostBatches() > 0 || m_storage->concurrentTursoExportFailed())
	{
		const std::string degraded =
			"Indexing health: the concurrent ingest DEGRADED this run (" +
			std::to_string(m_storage->concurrentTursoLostBatches()) + " lost batch(es)" +
			(m_storage->concurrentTursoExportFailed() ? ", export FAILED" : "") +
			"). The index is missing data — re-run with the fan-out disabled "
			"(\"indexing/multi_group_fan_out\": \"off\") for a serial re-index.";
		LOG_WARNING(degraded);
		MessageStatus(degraded, true, false).dispatch();
	}
#endif
	// The report holds no thread while it waits for the decision: run() waits for it.
	m_decision = std::make_shared<Decision>();
	m_dialogView->finishedIndexingDialog(
		indexedSourceFileCount,
		sourceFileCount,
		stats.completedFileCount,
		stats.fileCount,
		static_cast<float>(time),
		errorInfo,
		interruptedIndexing,
		[decision = m_decision](DatabasePolicy policy) {
			decision->policy = static_cast<int>(policy);
			decision->decided.notify();
		});
}
