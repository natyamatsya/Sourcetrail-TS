#ifndef TASK_BUILD_INDEX_H
#define TASK_BUILD_INDEX_H

#include "language_package_flags.h"
#include "IndexerClusterPlan.h"
#include "MessageIndexingInterrupted.h"
#include "MessageListener.h"
#include "StdexecPrelude.h"	// stdexec::inplace_stop_source / inplace_stop_callback
#include "Task.h"
#include "InterprocessBackend.h"

#include <atomic>
#include <chrono>
#include <map>
#include <optional>
#include <thread>

class DialogView;
class StorageProvider;
class IndexerCommandList;

class TaskBuildIndex
	: public Task
	, public MessageListener<MessageIndexingInterrupted>
{
public:
	//! A non-empty cxxClusterPlan (fan-out S3) overrides processCount: one C++
	//! subprocess per planned slot, each pinned to its cluster's source group.
	//! An empty plan gives processCount unpinned subprocesses (legacy).
	//! rustSupervisorCount (crate fan-out R1) spawns that many Rust supervisor
	//! threads, each babysitting one subprocess per crate command; they share
	//! the crate queue via accept-any pops. 1 = today's exact behavior.
	TaskBuildIndex(
		size_t processCount,
		std::shared_ptr<StorageProvider> storageProvider,
		std::shared_ptr<DialogView> dialogView,
		const std::string& appUUID,
		const std::vector<IndexerClusterEntry>& cxxClusterPlan = {},
		size_t rustSupervisorCount = 1);

	~TaskBuildIndex() override;

protected:
	void doEnter(std::shared_ptr<Blackboard> blackboard) override;
	TaskState doUpdate(std::shared_ptr<Blackboard> blackboard) override;
	void doExit(std::shared_ptr<Blackboard> blackboard) override;
	void doReset(std::shared_ptr<Blackboard> blackboard) override;
	void terminate() override;

	void handleMessage(MessageIndexingInterrupted* message) override;
	
	void runIndexerProcess(ProcessId processId, const std::string& logFilePath);
	void runRustIndexerProcess(ProcessId processId, const std::string& logFilePath);
	void runSwiftIndexerProcess(ProcessId processId, const std::string& logFilePath);
	bool fetchIntermediateStorages(std::shared_ptr<Blackboard> blackboard);
	void logIndexingSummary(const std::shared_ptr<Blackboard>& blackboard) const;
	void updateIndexingDialog(
		std::shared_ptr<Blackboard> blackboard, const std::vector<FilePath>& sourcePaths);

	static const std::string s_processName;

	std::shared_ptr<IndexerCommandList> m_indexerCommandList;
	std::shared_ptr<StorageProvider> m_storageProvider;
	std::shared_ptr<DialogView> m_dialogView;
	const std::string m_appUUID;

	//! Per-subprocess source-group pins (fan-out S2): a C++ indexer subprocess
	//! whose ProcessId maps to a non-empty group id only pops commands of that
	//! group. Unmapped processes accept any group (legacy behavior). Populated
	//! from the cluster plan (S3) in the constructor.
	std::map<ProcessId, std::string> m_processGroupIds;

	IndexingStatusManagerImpl m_interprocessIndexingStatusManager;
	std::atomic<bool> m_indexerCommandQueueStopped = false;
	size_t m_processCount;

	// Cooperative cancellation: request_stop() replaces the old racy `m_interrupted`
	// bool. A stop callback (registered in doEnter) raises the IPC interrupt flag so
	// subprocesses exit gracefully; the babysitter loops observe stop_requested().
	stdexec::inplace_stop_source m_stopSource;

	size_t m_indexingFileCount = 0;
	size_t m_lastWatchdogIndexedSourceFileCount = 0;
	size_t m_lastWatchdogIndexingFileCount = 0;
	std::chrono::steady_clock::time_point m_lastWatchdogProgressTime;
	std::chrono::steady_clock::time_point m_lastWatchdogLogTime;
	std::vector<FilePath> m_lastKnownIndexingFiles;

	// instrumentation for the B2 sharding gate (see logIndexingSummary)
	std::chrono::steady_clock::time_point m_indexingStartTime;
	size_t m_throttleStallCount = 0;
	long long m_throttleStallMs = 0;

	// Supervisor threads (one per indexer subprocess), auto-joined on destruction.
	// Deterministic teardown relies on cooperative stop (m_stopSource) + the kill
	// fallback in doExit/dtor so these joins always complete promptly.
	std::vector<std::jthread> m_processThreads;
	std::vector<std::shared_ptr<IntermediateStorageManagerImpl>>
		m_interprocessIntermediateStorageManagers;
	//! One per Rust supervisor (crate fan-out R1): ProcessIds
	//! m_processCount+1 .. m_processCount+m_rustSupervisorCount.
	std::vector<std::shared_ptr<IntermediateStorageManagerImpl>> m_rustStorageManagers;
	std::shared_ptr<IntermediateStorageManagerImpl> m_swiftStorageManager;
	size_t m_rustSupervisorCount = 1;

	std::atomic<size_t> m_runningThreadCount = 0;

	// Fires when m_stopSource is stopped: raise the IPC interrupt flag so the indexer
	// subprocesses stop gracefully. Declared last so it unregisters before m_stopSource
	// is destroyed.
	struct StopCallback
	{
		IndexingStatusManagerImpl* statusManager;
		void operator()() const noexcept
		{
			try
			{
				statusManager->setIndexingInterrupted(true);
			}
			catch (...)
			{
			}
		}
	};
	std::optional<stdexec::inplace_stop_callback<StopCallback>> m_stopCallback;
};

#endif	  // TASK_PARSE_H
