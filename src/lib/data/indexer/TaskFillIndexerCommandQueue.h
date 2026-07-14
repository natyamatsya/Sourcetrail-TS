#ifndef TASK_FILL_INDEXER_COMMAND_QUEUE_H
#define TASK_FILL_INDEXER_COMMAND_QUEUE_H

#include <queue>
#include <string>
#include <unordered_set>
#include <vector>

#include "language_package_flags.h"

#include "MessageIndexingInterrupted.h"
#include "MessageListener.h"
#include "Task.h"

#include "InterprocessBackend.h"

class CombinedIndexerCommandProvider;
class IndexerCommand;
class IndexerCommandProvider;

class TaskFillIndexerCommandsQueue
	: public Task
	, public MessageListener<MessageIndexingInterrupted>
{
public:
	struct DeduplicationState
	{
		std::unordered_set<std::string> seenWorkingDirectories;
		size_t skippedCount = 0;

		void reset()
		{
			seenWorkingDirectories.clear();
			skippedCount = 0;
		}
	};

	TaskFillIndexerCommandsQueue(
		const std::string& appUUID,
		std::unique_ptr<IndexerCommandProvider> indexerCommandProvider,
		size_t maximumQueueSize);

protected:
	void doEnter(std::shared_ptr<Blackboard> blackboard) override;
	TaskState doUpdate(std::shared_ptr<Blackboard> blackboard) override;
	void doExit(std::shared_ptr<Blackboard> blackboard) override;
	void doReset(std::shared_ptr<Blackboard> blackboard) override;
	void terminate() override;

	void handleMessage(MessageIndexingInterrupted* message) override;

	bool fillCommandQueue(std::shared_ptr<Blackboard> blackboard);

private:
	//! Consume up to `count` commands (legacy path: any group, ordered by the
	//! size-partitioned file-path queue). Caller holds m_commandsMutex.
	void consumeCommands(
		size_t count,
		std::vector<std::shared_ptr<IndexerCommand>>& commands,
		size_t& skippedRust,
		size_t& skippedSwift);
	//! Group-aware consume (fan-out S3): top up every source group to
	//! m_maximumQueueSize queued commands so pinned subprocesses and the
	//! language supervisors never starve on another group's backlog.
	//! Caller holds m_commandsMutex.
	void consumeCommandsGrouped(
		std::vector<std::shared_ptr<IndexerCommand>>& commands,
		size_t& skippedRust,
		size_t& skippedSwift);
	bool applyLanguageDeduplication(
		const std::shared_ptr<IndexerCommand>& command, size_t& skippedRust, size_t& skippedSwift);

	std::unique_ptr<IndexerCommandProvider> m_indexerCommandProvider;
	//! Non-null when the provider combines >= 2 source groups: enables the
	//! group-aware fill. Points into m_indexerCommandProvider.
	CombinedIndexerCommandProvider* m_combinedProvider = nullptr;
	std::vector<std::string> m_sourceGroupIds;

	IndexerCommandManagerImpl m_indexerCommandManager;

	const size_t m_maximumQueueSize;

	std::queue<FilePath> m_filePathQueue;
	std::mutex m_commandsMutex;

	DeduplicationState m_rustDedup;
	DeduplicationState m_swiftDedup;

	bool m_interrupted = false;
};

#endif	  // TASK_FILL_INDEXER_COMMAND_QUEUE_H
