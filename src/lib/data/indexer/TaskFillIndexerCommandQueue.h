#ifndef TASK_FILL_INDEXER_COMMAND_QUEUE_H
#define TASK_FILL_INDEXER_COMMAND_QUEUE_H

#include <queue>
#include <string>
#include <unordered_set>

#include "language_packages.h"

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

#if BUILD_RUST_LANGUAGE_PACKAGE
	std::unordered_set<std::string> m_seenRustWorkingDirectories;
	size_t m_skippedRustCommandCount = 0;
#endif

	bool m_interrupted = false;
};

#endif	  // TASK_FILL_INDEXER_COMMAND_QUEUE_H
