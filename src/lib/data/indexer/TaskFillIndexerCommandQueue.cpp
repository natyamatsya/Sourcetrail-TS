#include "TaskFillIndexerCommandQueue.h"

#include "Blackboard.h"
#include "FileSystem.h"
#include "IndexerCommand.h"
#include "IndexerCommandRust.h"
#include "IndexerCommandSwift.h"
#include "IndexerCommandProvider.h"
#include "logging.h"
#include "utilityFile.h"

namespace
{
template <bool Enabled, typename CmdType>
bool deduplicateByWorkingDir(
	TaskFillIndexerCommandsQueue::DeduplicationState& state,
	const std::shared_ptr<IndexerCommand>& cmd,
	size_t& skipped)
{
	if constexpr (!Enabled)
		return false;
	const auto* typed = dynamic_cast<const CmdType*>(cmd.get());
	if (!typed)
		return false;
	if (!state.seenWorkingDirectories.insert(typed->getWorkingDirectory().str()).second)
	{
		skipped++;
		return true;
	}
	return false;
}
}  // namespace

TaskFillIndexerCommandsQueue::TaskFillIndexerCommandsQueue(
	const std::string& appUUID,
	std::unique_ptr<IndexerCommandProvider> indexerCommandProvider,
	size_t maximumQueueSize)
	: m_indexerCommandProvider(std::move(indexerCommandProvider))
	, m_indexerCommandManager(appUUID, ProcessId::NONE, true)
	, m_maximumQueueSize(maximumQueueSize)
{
}

void TaskFillIndexerCommandsQueue::doEnter(std::shared_ptr<Blackboard> blackboard)
{
	{
		std::lock_guard<std::mutex> lock(m_commandsMutex);

		if constexpr (language_packages::buildRustLanguagePackage)
			m_rustDedup.reset();
		if constexpr (language_packages::buildSwiftLanguagePackage)
			m_swiftDedup.reset();

		for (const FilePath& filePath:
			 utility::partitionFilePathsBySize(m_indexerCommandProvider->getAllSourceFilePaths(), 2))
		{
			m_filePathQueue.emplace(filePath);
		}
	}

	fillCommandQueue(blackboard);

	blackboard->set<bool>("indexer_command_queue_started", true);
}

Task::TaskState TaskFillIndexerCommandsQueue::doUpdate(std::shared_ptr<Blackboard> blackboard)
{
	using enum Task::TaskState;
	if (m_interrupted)
	{
		return STATE_FAILURE;
	}

	if (!fillCommandQueue(blackboard))
	{
		std::lock_guard<std::mutex> lock(m_commandsMutex);

		if (m_indexerCommandProvider->empty())
		{
			return STATE_SUCCESS;
		}
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	return STATE_RUNNING;
}

void TaskFillIndexerCommandsQueue::doExit(std::shared_ptr<Blackboard> blackboard)
{
	using enum Task::TaskState;
	if constexpr (language_packages::buildRustLanguagePackage)
		if (m_rustDedup.skippedCount)
			LOG_INFO_STREAM(<< "Skipped " << m_rustDedup.skippedCount
				<< " duplicate Rust crate commands in this indexing run.");
	if constexpr (language_packages::buildSwiftLanguagePackage)
		if (m_swiftDedup.skippedCount)
			LOG_INFO_STREAM(<< "Skipped " << m_swiftDedup.skippedCount
				<< " duplicate Swift package commands in this indexing run.");

	blackboard->set<bool>("indexer_command_queue_stopped", true);

	// The command queue won't grow any further: wake subprocesses blocked waiting
	// for a command so they observe the stop and exit promptly, rather than waiting
	// out their poll-timeout backstop.
	m_indexerCommandManager.notifyWaiters();
}

void TaskFillIndexerCommandsQueue::doReset(std::shared_ptr<Blackboard>  /*blackboard*/)
{
	m_interrupted = false;

	if constexpr (language_packages::buildRustLanguagePackage)
		m_rustDedup.reset();
	if constexpr (language_packages::buildSwiftLanguagePackage)
		m_swiftDedup.reset();
}

void TaskFillIndexerCommandsQueue::terminate()
{
	m_interrupted = true;
}

void TaskFillIndexerCommandsQueue::handleMessage(MessageIndexingInterrupted*  /*message*/)
{
	using enum Task::TaskState;
	std::lock_guard<std::mutex> lock(m_commandsMutex);

	LOG_INFO(
		"Discarding remaining " +
		std::to_string(
			m_indexerCommandProvider->size() + m_indexerCommandManager.indexerCommandCount()) +
		" indexer commands.");

	std::queue<FilePath> empty;
	std::swap(m_filePathQueue, empty);

	m_indexerCommandProvider->clear();
	m_indexerCommandManager.clearIndexerCommands();

	LOG_INFO(
		"Remaining: " +
		std::to_string(
			m_indexerCommandProvider->size() + m_indexerCommandManager.indexerCommandCount()) +
		".");
}

bool TaskFillIndexerCommandsQueue::fillCommandQueue(std::shared_ptr<Blackboard> blackboard)
{
	using enum Task::TaskState;
	size_t refillAmount = m_maximumQueueSize - m_indexerCommandManager.indexerCommandCount();
	if (!refillAmount)
	{
		return false;
	}

	std::lock_guard<std::mutex> lock(m_commandsMutex);
	std::vector<std::shared_ptr<IndexerCommand>> commands;
	size_t skippedRust = 0;
	size_t skippedSwift = 0;

	while (!m_indexerCommandProvider->empty() && commands.size() < refillAmount)
	{
		std::shared_ptr<IndexerCommand> command;
		if (!m_filePathQueue.empty())
		{
			command = m_indexerCommandProvider->consumeCommandForSourceFilePath(m_filePathQueue.front());
			m_filePathQueue.pop();
		}
		else
		{
			command = m_indexerCommandProvider->consumeCommand();
		}

		if (!command)
			continue;

		if (deduplicateByWorkingDir<language_packages::buildRustLanguagePackage, IndexerCommandRust>(
			m_rustDedup, command, skippedRust))
			continue;
		if (deduplicateByWorkingDir<language_packages::buildSwiftLanguagePackage, IndexerCommandSwift>(
			m_swiftDedup, command, skippedSwift))
			continue;

		commands.push_back(command);
	}

	const size_t skippedCommands = skippedRust + skippedSwift;
	if (skippedCommands)
	{
		m_rustDedup.skippedCount  += skippedRust;
		m_swiftDedup.skippedCount += skippedSwift;
		if (blackboard)
			blackboard->update<int>(
				"source_file_count",
				[n = static_cast<int>(skippedCommands)](int count)
				{
					const int newCount = count - n;
					return newCount >= 0 ? newCount : 0;
				});
		if (skippedRust)
			LOG_INFO_STREAM(<< "Skipping " << skippedRust
				<< " duplicate Rust crate commands while filling queue.");
		if (skippedSwift)
			LOG_INFO_STREAM(<< "Skipping " << skippedSwift
				<< " duplicate Swift package commands while filling queue.");
	}

	if (commands.empty())
		return false;

	m_indexerCommandManager.pushIndexerCommands(commands);
	return true;
}
