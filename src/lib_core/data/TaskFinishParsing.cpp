#include "TaskFinishParsing.h"

#include "Blackboard.h"
#include "DialogView.h"
#ifndef SRCTRL_MODULE_BUILD
#include "MessageIndexingFinished.h"
#include "MessageIndexingStatus.h"
#include "MessageStatus.h"
#endif
#include "PersistentStorage.h"
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

void TaskFinishParsing::terminate()
{
	m_dialogView->clearDialogs();

	MessageStatus("An unknown exception was thrown during indexing.", true, false).dispatch();
	// The terminal MessageIndexingFinished is owned by the pipeline root (the TaskFinally wrapped
	// around the whole tree in Project::buildIndexImpl) -- dispatching it here too would double
	// the terminal event on termination.
}

void TaskFinishParsing::doEnter(std::shared_ptr<Blackboard>  /*blackboard*/)
{
	using enum Task::TaskState;
	using enum DatabasePolicy;
#ifdef SOURCETRAIL_TURSO_CONCURRENT
	// Drain the concurrent Turso writers before any read of the finished database.
	m_storage->finishConcurrentTurso();
#endif
	m_storage->setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);
}

Task::TaskState TaskFinishParsing::doUpdate(std::shared_ptr<Blackboard> blackboard)
{
	using enum Task::TaskState;
	using enum DatabasePolicy;
	TimeStamp start = TimeStamp::now();

	m_dialogView->showUnknownProgressDialog("Finish Indexing", "Optimizing database");
	m_storage->setBulkWritePragmas(false);	  // restore SYNCHRONOUS=NORMAL before the DB goes live
	m_storage->optimizeMemory();
	m_dialogView->hideUnknownProgressDialog();

	double time = TimeStamp::durationSeconds(start);

	if (blackboard->exists("clear_time"))
	{
		float clearTime = 0;
		blackboard->get("clear_time", clearTime);
		time += clearTime;
	}

	if (blackboard->exists("index_time"))
	{
		float indexTime = 0;
		blackboard->get("index_time", indexTime);
		time += indexTime;
	}

	int indexedSourceFileCount = 0;
	blackboard->get("indexed_source_file_count", indexedSourceFileCount);

	int sourceFileCount = 0;
	blackboard->get("source_file_count", sourceFileCount);

	bool interruptedIndexing = false;
	blackboard->get("interrupted_indexing", interruptedIndexing);

	ErrorCountInfo errorInfo = m_storage->getErrorCount();

	std::string status;
	status += "Finished indexing: ";
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
	DatabasePolicy policy = m_dialogView->finishedIndexingDialog(
		indexedSourceFileCount,
		sourceFileCount,
		stats.completedFileCount,
		stats.fileCount,
		static_cast<float>(time),
		errorInfo,
		interruptedIndexing);

	MessageIndexingStatus(false).dispatch();

	if (policy == DATABASE_POLICY_KEEP)
	{
		blackboard->set("keep_database", true);
	}
	else if (policy == DATABASE_POLICY_DISCARD)
	{
		blackboard->set("discard_database", true);
	}
	else if (policy == DATABASE_POLICY_REFRESH)
	{
		blackboard->set("keep_database", true);
		blackboard->set("refresh_database", true);
	}

	return STATE_SUCCESS;
}

void TaskFinishParsing::doExit(std::shared_ptr<Blackboard>  /*blackboard*/)
{
	m_storage.reset();
}

void TaskFinishParsing::doReset(std::shared_ptr<Blackboard>  /*blackboard*/) {}
