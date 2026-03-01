#include "TaskFillIndexerCommandQueue.h"

#include "Blackboard.h"
#include "FileSystem.h"
#include "IndexerCommand.h"
#include "IndexerCommandRust.h"
#include "IndexerCommandSwift.h"
#include "IndexerCommandProvider.h"
#include "logging.h"
#include "utilityFile.h"

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
		{
			m_seenRustWorkingDirectories.clear();
			m_skippedRustCommandCount = 0;
		}
		if constexpr (language_packages::buildSwiftLanguagePackage)
		{
			m_seenSwiftWorkingDirectories.clear();
			m_skippedSwiftCommandCount = 0;
		}

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
	if constexpr (language_packages::buildRustLanguagePackage)
	{
		if (m_skippedRustCommandCount)
			LOG_INFO_STREAM(
				<< "Skipped " << m_skippedRustCommandCount
				<< " duplicate Rust crate commands in this indexing run.");
	}
	if constexpr (language_packages::buildSwiftLanguagePackage)
	{
		if (m_skippedSwiftCommandCount)
			LOG_INFO_STREAM(
				<< "Skipped " << m_skippedSwiftCommandCount
				<< " duplicate Swift package commands in this indexing run.");
	}

	blackboard->set<bool>("indexer_command_queue_stopped", true);
}

void TaskFillIndexerCommandsQueue::doReset(std::shared_ptr<Blackboard>  /*blackboard*/)
{
	m_interrupted = false;

	if constexpr (language_packages::buildRustLanguagePackage)
	{
		m_seenRustWorkingDirectories.clear();
		m_skippedRustCommandCount = 0;
	}
	if constexpr (language_packages::buildSwiftLanguagePackage)
	{
		m_seenSwiftWorkingDirectories.clear();
		m_skippedSwiftCommandCount = 0;
	}
}

void TaskFillIndexerCommandsQueue::terminate()
{
	m_interrupted = true;
}

void TaskFillIndexerCommandsQueue::handleMessage(MessageIndexingInterrupted*  /*message*/)
{
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
	size_t refillAmount = m_maximumQueueSize - m_indexerCommandManager.indexerCommandCount();
	if (!refillAmount)
	{
		return false;
	}

	std::lock_guard<std::mutex> lock(m_commandsMutex);
	std::vector<std::shared_ptr<IndexerCommand>> commands;
	size_t skippedRustCommands = 0;
	size_t skippedSwiftCommands = 0;

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

		if constexpr (language_packages::buildRustLanguagePackage)
		{
			if (command->getIndexerCommandType() == INDEXER_COMMAND_RUST)
			{
				const auto* rustCommand = dynamic_cast<const IndexerCommandRust*>(command.get());
				if (rustCommand)
				{
					const std::string workingDirectory = rustCommand->getWorkingDirectory().str();
					if (!m_seenRustWorkingDirectories.insert(workingDirectory).second)
					{
						skippedRustCommands++;
						continue;
					}
				}
			}
		}

		if constexpr (language_packages::buildSwiftLanguagePackage)
		{
			if (command->getIndexerCommandType() == INDEXER_COMMAND_SWIFT)
			{
				const auto* swiftCommand = dynamic_cast<const IndexerCommandSwift*>(command.get());
				if (swiftCommand)
				{
					const std::string workingDirectory = swiftCommand->getWorkingDirectory().str();
					if (!m_seenSwiftWorkingDirectories.insert(workingDirectory).second)
					{
						skippedSwiftCommands++;
						continue;
					}
				}
			}
		}

		commands.push_back(command);
	}

	const size_t skippedCommands = skippedRustCommands + skippedSwiftCommands;
	if (skippedCommands)
	{
		if constexpr (language_packages::buildRustLanguagePackage)
			m_skippedRustCommandCount += skippedRustCommands;
		if constexpr (language_packages::buildSwiftLanguagePackage)
			m_skippedSwiftCommandCount += skippedSwiftCommands;
		if (blackboard)
			blackboard->update<int>(
				"source_file_count",
				[n = static_cast<int>(skippedCommands)](int count)
				{
					const int newCount = count - n;
					return newCount >= 0 ? newCount : 0;
				});
		if constexpr (language_packages::buildRustLanguagePackage)
		{
			if (skippedRustCommands)
				LOG_INFO_STREAM(
					<< "Skipping " << skippedRustCommands
					<< " duplicate Rust crate commands while filling queue.");
		}
		if constexpr (language_packages::buildSwiftLanguagePackage)
		{
			if (skippedSwiftCommands)
				LOG_INFO_STREAM(
					<< "Skipping " << skippedSwiftCommands
					<< " duplicate Swift package commands while filling queue.");
		}
	}

	if (commands.empty())
		return false;

	m_indexerCommandManager.pushIndexerCommands(commands);
	return true;
}
