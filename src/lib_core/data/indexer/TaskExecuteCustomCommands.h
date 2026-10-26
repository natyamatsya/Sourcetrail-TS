#ifndef TASK_EXECUTE_CUSTOM_COMMANDS_H
#define TASK_EXECUTE_CUSTOM_COMMANDS_H

#include <atomic>
#include <set>
#include <vector>

#include "ErrorCountInfo.h"
#include "FilePath.h"
#include "MessageIndexingInterrupted.h"
#include "MessageListener.h"
#include "TimeStamp.h"

class DialogView;
class IndexerCommand;
class IndexerCommandProvider;
class PersistentStorage;
struct IndexingRunState;

// Runs the custom source groups' commands, side by side on the work pool, and merges
// what they wrote into the target.
class TaskExecuteCustomCommands: public MessageListener<MessageIndexingInterrupted>
{
public:
	TaskExecuteCustomCommands(
		std::unique_ptr<IndexerCommandProvider> indexerCommandProvider,
		std::shared_ptr<PersistentStorage> storage,
		std::shared_ptr<DialogView> dialogView,
		size_t indexerThreadCount,
		const FilePath& projectDirectory);

	void run(IndexingRunState& state);

private:
	void collectCommands();
	void runCommands(IndexingRunState& state);

	void handleMessage(MessageIndexingInterrupted* message) override;

	void executeParallelIndexerCommands(int threadId, IndexingRunState& state);
	void runIndexerCommand(
		std::shared_ptr<IndexerCommand> indexerCommand,
		IndexingRunState& state,
		std::shared_ptr<PersistentStorage> storage);

	std::unique_ptr<IndexerCommandProvider> m_indexerCommandProvider;
	std::shared_ptr<PersistentStorage> m_storage;
	std::shared_ptr<DialogView> m_dialogView;
	const size_t m_indexerThreadCount;
	const FilePath m_projectDirectory;

	std::atomic<bool> m_interrupted = false;	// set by the message thread, read by the workers
	size_t m_indexerCommandCount;
	std::vector<std::shared_ptr<IndexerCommand>> m_serialCommands;
	std::vector<std::shared_ptr<IndexerCommand>> m_parallelCommands;
	std::mutex m_parallelCommandsMutex;
	ErrorCountInfo m_errorCount;
	std::mutex m_errorCountMutex;
	FilePath m_targetDatabaseFilePath;
	std::set<FilePath> m_sourceDatabaseFilePaths;
	std::mutex m_sourceDatabaseFilePathsMutex;
};

#endif	  // TASK_EXECUTE_CUSTOM_COMMANDS_H
