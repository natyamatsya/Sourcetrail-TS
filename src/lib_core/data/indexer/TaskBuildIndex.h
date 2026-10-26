#ifndef TASK_BUILD_INDEX_H
#define TASK_BUILD_INDEX_H

#include "language_package_flags.h"
#include "IndexerClusterPlan.h"
#include "IndexerCommandType.h"
#include "MessageIndexingInterrupted.h"
#include "MessageListener.h"
#include "StdexecPrelude.h"	// stdexec::inplace_stop_source / inplace_stop_callback
#include "InterprocessBackend.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

#include "ProcessMonitor.h"

#include <exec/task.hpp>

class DialogView;
class StorageProvider;
class IndexerCommandList;
struct IndexingRunState;

// The indexing stream: runs the indexer subprocesses on the command queue's commands,
// and hands what they stored on to the merge and the writer.
class TaskBuildIndex: public MessageListener<MessageIndexingInterrupted>
{
public:
	//! A non-empty cxxClusterPlan (fan-out S3) overrides processCount: one C++
	//! subprocess per planned slot, each pinned to its cluster's source group.
	//! An empty plan gives processCount unpinned subprocesses (legacy).
	//! rustSupervisorCount (crate fan-out R1) spawns that many Rust supervisor
	//! threads, each babysitting one subprocess per crate command; they share
	//! the crate queue via accept-any pops. 1 = today's exact behavior.
	//! swiftSupervisorCount (SW6) is the Swift analog, one supervisor per
	//! package command up to the cap.
	TaskBuildIndex(
		size_t processCount,
		std::shared_ptr<StorageProvider> storageProvider,
		std::shared_ptr<DialogView> dialogView,
		const std::string& appUUID,
		const std::vector<IndexerClusterEntry>& cxxClusterPlan = {},
		size_t rustSupervisorCount = 1,
		size_t swiftSupervisorCount = 1,
		size_t zigSupervisorCount = 1);

	~TaskBuildIndex() override;

	//! Starts the indexers, looks at them every 50 ms until the queue is done (or the
	//! run interrupted) and they ended, and hands on the rest. Throws when it failed.
	//! Stopped, it ends at once: its destructor stops what still runs.
	exec::task<void> run(IndexingRunState& state);

protected:
	enum class Look
	{
		Running,
		Done,
		Failed
	};
	void start(IndexingRunState& state);
	Look look(IndexingRunState& state);
	void end(IndexingRunState& state);

	void handleMessage(MessageIndexingInterrupted* message) override;

	// One indexer subprocess's supervision, on the process monitor's thread: the
	// subprocess is launched again while it is needed, tolerating up to
	// maxConsecutiveFailures transient failures before the whole run is interrupted
	// -- so one crash never aborts indexing. No thread waits for a subprocess.
	struct Supervisor;

	// The C++ indexer's supervisor: launches while the command queue runs (or the
	// last run failed). Empty when the indexer is missing (indexing stops).
	std::shared_ptr<Supervisor> cxxSupervisor(ProcessId processId, const std::string& logFilePath);
	// An out-of-process language indexer's (Rust, Swift, Zig): launches while
	// commands of its type remain. Empty when the indexer is missing.
	std::shared_ptr<Supervisor> externalSupervisor(
		ProcessId processId,
		const std::string& logFilePath,
		const FilePath& indexerPath,
		IndexerCommandType commandType,
		const std::string& humanName);
	void startSupervisor(std::shared_ptr<Supervisor> supervisor);
	// Launches its subprocess if it is still needed, else ends it.
	void launch(const std::shared_ptr<Supervisor>& supervisor);
	void ended(const std::shared_ptr<Supervisor>& supervisor, const utility::ProcessOutput& output);
	// Launches again once `delay` has passed.
	void again(const std::shared_ptr<Supervisor>& supervisor, std::chrono::milliseconds delay);
	void finish(Supervisor& supervisor);
	// Whether the supervisors all ended within `limit`.
	bool waitForSupervisors(std::chrono::milliseconds limit);
	// Interrupts the run: no more launches, the subprocesses killed.
	void interruptAll();
	void killProcesses();
	// Drains the subprocesses' storages and hands them on; whether any came or went.
	bool fetchIntermediateStorages(IndexingRunState& state);
	// Hands pending storages to the provider: while the writer keeps up, or `all`.
	size_t handOnStorages(IndexingRunState& state, bool all);
	void logIndexingSummary(const IndexingRunState& state) const;
	void updateIndexingDialog(const IndexingRunState& state, const std::vector<FilePath>& sourcePaths);

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
	// bool. A stop callback (registered in start()) raises the IPC interrupt flag so
	// subprocesses exit gracefully; the babysitter loops observe stop_requested().
	stdexec::inplace_stop_source m_stopSource;
	std::uint64_t m_cause = 0;	// what the run works for: its supervisors carry it on

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

	// Drained, not yet handed to the provider: the writer was behind.
	std::deque<std::shared_ptr<IntermediateStorage>> m_pendingStorages;

	// Runs the indexer subprocesses and tells when each ended, from one thread.
	// Created as indexing starts.
	std::unique_ptr<ProcessMonitor> m_processes;
	std::vector<std::shared_ptr<IntermediateStorageManagerImpl>>
		m_interprocessIntermediateStorageManagers;
	//! One per Rust supervisor (crate fan-out R1): ProcessIds
	//! m_processCount+1 .. m_processCount+m_rustSupervisorCount.
	std::vector<std::shared_ptr<IntermediateStorageManagerImpl>> m_rustStorageManagers;
	//! One Swift storage manager per supervisor (SW6), ids following the Rust
	//! supervisors: m_processCount+m_rustSupervisorCount+1 ..
	std::vector<std::shared_ptr<IntermediateStorageManagerImpl>> m_swiftStorageManagers;
	//! One Zig storage manager per supervisor, ids following the Swift
	//! supervisors: m_processCount+m_rustSupervisorCount+m_swiftSupervisorCount+1 ..
	std::vector<std::shared_ptr<IntermediateStorageManagerImpl>> m_zigStorageManagers;
	size_t m_rustSupervisorCount = 1;
	size_t m_swiftSupervisorCount = 1;
	size_t m_zigSupervisorCount = 1;

	// Supervisors that have not ended; the teardown waits for none.
	std::atomic<size_t> m_runningSupervisorCount = 0;
	std::mutex m_supervisorsMutex;
	std::condition_variable m_supervisorsEnded;

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

#endif	  // TASK_BUILD_INDEX_H
