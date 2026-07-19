#include "TaskFillIndexerCommandQueue.h"

#include <algorithm>
#include <map>

#include "Blackboard.h"
#include "CombinedIndexerCommandProvider.h"
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
	const CmdType* typed = cmd->template target<CmdType>();
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
	// With >= 2 source groups, fill group-aware (fan-out S3): the queue holds up
	// to m_maximumQueueSize commands PER group, so subprocesses pinned to one
	// group (and the Rust/Swift supervisors) always find their work regardless
	// of another group's backlog.
	if (auto* combined = dynamic_cast<CombinedIndexerCommandProvider*>(m_indexerCommandProvider.get()))
	{
		std::vector<std::string> groupIds = combined->getSourceGroupIds();
		if (groupIds.size() >= 2)
		{
			m_combinedProvider = combined;
			m_sourceGroupIds = std::move(groupIds);
		}
	}
}

void TaskFillIndexerCommandsQueue::doEnter(std::shared_ptr<Blackboard> blackboard)
{
	{
		std::lock_guard<std::mutex> lock(m_commandsMutex);

		if constexpr (language_packages::buildRustLanguagePackage)
			m_rustDedup.reset();
		if constexpr (language_packages::buildSwiftLanguagePackage)
			m_swiftDedup.reset();

		// The size-partitioned path order only drives the legacy (single-group)
		// fill; the group-aware fill consumes per group in provider order.
		if (m_combinedProvider == nullptr)
		{
			for (const FilePath& filePath:
				 utility::partitionFilePathsBySize(m_indexerCommandProvider->getAllSourceFilePaths(), 2))
			{
				m_filePathQueue.emplace(filePath);
			}
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

bool TaskFillIndexerCommandsQueue::applyLanguageDeduplication(
	const std::shared_ptr<IndexerCommand>& command, size_t& skippedRust, size_t& skippedSwift)
{
	if (deduplicateByWorkingDir<language_packages::buildRustLanguagePackage, IndexerCommandRust>(
		m_rustDedup, command, skippedRust))
		return true;
	if (deduplicateByWorkingDir<language_packages::buildSwiftLanguagePackage, IndexerCommandSwift>(
		m_swiftDedup, command, skippedSwift))
		return true;
	return false;
}

void TaskFillIndexerCommandsQueue::consumeCommands(
	size_t count,
	std::vector<std::shared_ptr<IndexerCommand>>& commands,
	size_t& skippedRust,
	size_t& skippedSwift)
{
	while (!m_indexerCommandProvider->empty() && commands.size() < count)
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

		if (applyLanguageDeduplication(command, skippedRust, skippedSwift))
			continue;

		commands.push_back(command);
	}
}

void TaskFillIndexerCommandsQueue::consumeCommandsGrouped(
	std::vector<std::shared_ptr<IndexerCommand>>& commands,
	size_t& skippedRust,
	size_t& skippedSwift)
{
	const std::map<std::string, size_t> queuedCounts =
		m_indexerCommandManager.indexerCommandCountsBySourceGroup();

	for (const std::string& groupId: m_sourceGroupIds)
	{
		const auto queuedIt = queuedCounts.find(groupId);
		size_t queued = queuedIt != queuedCounts.end() ? queuedIt->second : 0;

		while (queued < m_maximumQueueSize)
		{
			const std::shared_ptr<IndexerCommand> command =
				m_combinedProvider->consumeCommandForSourceGroup(groupId);
			if (!command)
				break;

			if (applyLanguageDeduplication(command, skippedRust, skippedSwift))
				continue;

			commands.push_back(command);
			queued++;
		}
	}
}

bool TaskFillIndexerCommandsQueue::fillCommandQueue(std::shared_ptr<Blackboard> blackboard)
{
	using enum Task::TaskState;
	std::vector<std::shared_ptr<IndexerCommand>> commands;
	size_t skippedRust = 0;
	size_t skippedSwift = 0;

	if (m_combinedProvider != nullptr)
	{
		std::lock_guard<std::mutex> lock(m_commandsMutex);
		consumeCommandsGrouped(commands, skippedRust, skippedSwift);
	}
	else
	{
		const size_t refillAmount =
			m_maximumQueueSize - std::min(m_maximumQueueSize, m_indexerCommandManager.indexerCommandCount());
		if (refillAmount == 0)
		{
			return false;
		}

		std::lock_guard<std::mutex> lock(m_commandsMutex);
		consumeCommands(refillAmount, commands, skippedRust, skippedSwift);
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
