#ifndef TASK_FILL_INDEXER_COMMAND_QUEUE_H
#define TASK_FILL_INDEXER_COMMAND_QUEUE_H

#include <queue>
#include <string>
#include <unordered_set>

#include "language_package_flags.h"

#include "MessageIndexingInterrupted.h"
#include "MessageListener.h"
#include "Task.h"

#include "InterprocessBackend.h"

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
	std::unique_ptr<IndexerCommandProvider> m_indexerCommandProvider;
	IndexerCommandManagerImpl m_indexerCommandManager;

	const size_t m_maximumQueueSize;

	std::queue<FilePath> m_filePathQueue;
	std::mutex m_commandsMutex;

	DeduplicationState m_rustDedup;
	DeduplicationState m_swiftDedup;

	bool m_interrupted = false;
};

#endif	  // TASK_FILL_INDEXER_COMMAND_QUEUE_H
