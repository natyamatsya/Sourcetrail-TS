#include "TaskBuildIndex.h"

#include "language_package_flags.h"
#include "AppPath.h"
#include "IndexerCommandType.h"
#include "Blackboard.h"
#include "DialogView.h"
#include "FileLogger.h"
#include "MessageIndexingStatus.h"
#include "ParserClientImpl.h"
#include "ScopedFunctor.h"
#include "StorageProvider.h"
#include "TimeStamp.h"
#include "UserPaths.h"
#include "utilityApp.h"
#include "utilityExpected.h"

using namespace utility;

namespace
{
	using enum Task::TaskState;
	using enum IndexerCommandType;
constexpr bool hasRustLanguagePackage{language_packages::buildRustLanguagePackage};
constexpr bool hasSwiftLanguagePackage{language_packages::buildSwiftLanguagePackage};

ProcessId rustIndexerProcessId(const size_t processCount)
{
	return static_cast<ProcessId>(processCount + 1);
}

[[maybe_unused]] ProcessId swiftIndexerProcessId(const size_t processCount)
{
	if constexpr (hasRustLanguagePackage)
		return static_cast<ProcessId>(processCount + 2);

	return static_cast<ProcessId>(processCount + 1);
}

enum class IndexerProcessErrorCode
{
	ExecutableMissing,
	ProcessStartFailed,
	ProcessExitedWithError
};

using IndexerProcessError = utility::ExpectedError<IndexerProcessErrorCode>;
using IndexerProcessResult = std::expected<void, IndexerProcessError>;

std::string buildProcessCommandLine(
	const FilePath& executablePath,
	const std::vector<std::string>& arguments)
{
	std::string commandLine = "\"" + executablePath.str() + "\"";
	for (const std::string& argument: arguments)
		commandLine += " \"" + argument + "\"";
	return commandLine;
}

IndexerProcessResult validateIndexerProcessOutput(
	const utility::ProcessOutput& processOutput,
	const ProcessId processId,
	const size_t launchAttempt,
	const std::string& processLabel,
	const std::string& commandLine)
{
	if (processOutput.exitCode == 0)
		return {};

	const std::string processInfo = processOutput.processInfo.empty() ? "" :
		"\nprocess info:\n" + processOutput.processInfo;

	if (processOutput.exitCode == -1)
	{
		return std::unexpected(utility::makeExpectedError(
			IndexerProcessErrorCode::ProcessStartFailed,
			processLabel + " " + to_string(processId) + " attempt " +
				std::to_string(launchAttempt) +
				" failed to start. command: " + commandLine + ". process error: " +
				processOutput.error + processInfo));
	}

	std::string message =
		processLabel + " " + to_string(processId) + " attempt " +
		std::to_string(launchAttempt) + " failed with exit code " +
		std::to_string(processOutput.exitCode) + ". command: " + commandLine + ".";
	if (!processOutput.output.empty())
		message += "\nprocess output:\n" + processOutput.output;
	message += processInfo;

	return std::unexpected(utility::makeExpectedError(
		IndexerProcessErrorCode::ProcessExitedWithError,
		message));
}

void logCxxIndexerFailureContext(
	IndexingStatusManagerImpl& indexingStatusManager,
	const ProcessId processId)
{
	const auto currentFileForProcess =
		indexingStatusManager.getCurrentSourceFilePathForProcess(processId);
	if (currentFileForProcess)
	{
		LOG_ERROR_STREAM(
			<< "CXX indexer failure context: process " << processId
			<< " was indexing: " << currentFileForProcess->str());
		return;
	}

	LOG_ERROR_STREAM(
		<< "CXX indexer failure context: process " << processId
		<< " has no tracked current source file.");

	const std::vector<FilePath> currentFiles = indexingStatusManager.getCurrentSourceFilePaths();
	if (currentFiles.empty())
	{
		LOG_ERROR_STREAM(
			<< "CXX indexer failure context: no active source files are currently tracked.");
		return;
	}

	for (const FilePath& currentFile: currentFiles)
		LOG_ERROR_STREAM(
			<< "CXX indexer failure context: active file in another process: "
			<< currentFile.str());
}
} // namespace

TaskBuildIndex::TaskBuildIndex(
	size_t processCount,
	std::shared_ptr<StorageProvider> storageProvider,
	std::shared_ptr<DialogView> dialogView,
	const std::string& appUUID)
	: m_storageProvider(storageProvider)
	, m_dialogView(dialogView)
	, m_appUUID(appUUID)
	, m_interprocessIndexingStatusManager(appUUID, ProcessId::NONE, true)
	, m_processCount(processCount)

{
}

void TaskBuildIndex::doEnter(std::shared_ptr<Blackboard> blackboard)
{
	m_interprocessIndexingStatusManager.setIndexingInterrupted(false);
	m_interprocessIndexingStatusManager.setQueueStopped(false);

	m_indexingFileCount = 0;
	m_lastWatchdogIndexedSourceFileCount = 0;
	m_lastWatchdogIndexingFileCount = 0;
	m_lastKnownIndexingFiles.clear();
	m_lastWatchdogProgressTime = std::chrono::steady_clock::now();
	m_lastWatchdogLogTime = m_lastWatchdogProgressTime;
	updateIndexingDialog(blackboard, std::vector<FilePath>());

	std::string logFilePath;
	Logger* logger = LogManager::getInstance()->getLoggerByType("FileLogger");
	if (logger != nullptr)
	{
		logFilePath = dynamic_cast<FileLogger*>(logger)->getLogFilePath().str();
	}

	// start indexer processes
	for (size_t i = 0; i < m_processCount; i++)
	{
		m_runningThreadCount++;

		const ProcessId processId = static_cast<ProcessId>(i + 1);	// 0 remains reserved for the main process

		m_interprocessIntermediateStorageManagers.push_back(std::make_shared<IntermediateStorageManagerImpl>(m_appUUID, processId, true));
		m_processThreads.push_back(
			new std::thread(&TaskBuildIndex::runIndexerProcess, this, processId, logFilePath));
	}

	// Start the Rust indexer process (one instance handles all Rust-typed commands).
	// It gets the next processId after the CXX indexers so it has its own storage channel.
	if constexpr (hasRustLanguagePackage)
	{
		const ProcessId rustProcessId{rustIndexerProcessId(m_processCount)};
		m_rustStorageManager = std::make_shared<IntermediateStorageManagerImpl>(
			m_appUUID, rustProcessId, true);
		m_runningThreadCount++;
		m_processThreads.push_back(
			new std::thread(&TaskBuildIndex::runRustIndexerProcess, this, rustProcessId, logFilePath));
	}

	if constexpr (hasSwiftLanguagePackage)
	{
		const ProcessId swiftProcessId{swiftIndexerProcessId(m_processCount)};
		m_swiftStorageManager = std::make_shared<IntermediateStorageManagerImpl>(
			m_appUUID, swiftProcessId, true);
		m_runningThreadCount++;
		m_processThreads.push_back(
			new std::thread(&TaskBuildIndex::runSwiftIndexerProcess, this, swiftProcessId, logFilePath));
	}

	blackboard->set<bool>("indexer_threads_started", true);
}

Task::TaskState TaskBuildIndex::doUpdate(std::shared_ptr<Blackboard> blackboard)
{
	using enum Task::TaskState;
	using enum IndexerCommandType;
	try
	{
		size_t runningThreadCount = m_runningThreadCount;

		blackboard->get<bool>("indexer_command_queue_stopped", m_indexerCommandQueueStopped);
		if (m_indexerCommandQueueStopped && !m_interprocessIndexingStatusManager.getQueueStopped())
			m_interprocessIndexingStatusManager.setQueueStopped(true);

		const std::vector<FilePath> indexingFiles = m_interprocessIndexingStatusManager.getCurrentlyIndexedSourceFilePaths();
		if (!indexingFiles.empty())
		{
			m_lastKnownIndexingFiles = indexingFiles;
			updateIndexingDialog(blackboard, indexingFiles);
		}

		if (m_indexerCommandQueueStopped && runningThreadCount == 0)
		{
			LOG_INFO_STREAM(<< "command queue stopped and no running threads. done.");
			return STATE_SUCCESS;
		}
		else if (m_interrupted)
		{
			LOG_INFO_STREAM(<< "interrupted indexing.");
			blackboard->set("interrupted_indexing", true);
			return STATE_SUCCESS;
		}

		if (fetchIntermediateStorages(blackboard))
		{
			updateIndexingDialog(blackboard, std::vector<FilePath>());
		}

		int indexedSourceFileCount = 0;
		blackboard->get("indexed_source_file_count", indexedSourceFileCount);
		const size_t currentIndexedSourceFileCount =
			indexedSourceFileCount > 0 ? static_cast<size_t>(indexedSourceFileCount) : 0;
		const size_t currentIndexingFileCount = m_indexingFileCount;
		const auto now = std::chrono::steady_clock::now();
		if (
			currentIndexedSourceFileCount > m_lastWatchdogIndexedSourceFileCount ||
			currentIndexingFileCount > m_lastWatchdogIndexingFileCount)
		{
			m_lastWatchdogIndexedSourceFileCount = currentIndexedSourceFileCount;
			m_lastWatchdogIndexingFileCount = currentIndexingFileCount;
			m_lastWatchdogProgressTime = now;
		}
		else if (
			!m_indexerCommandQueueStopped &&
			!m_interrupted &&
			runningThreadCount > 0 &&
			now - m_lastWatchdogProgressTime > std::chrono::seconds(30) &&
			now - m_lastWatchdogLogTime > std::chrono::seconds(10))
		{
			int sourceFileCount = 0;
			blackboard->get("source_file_count", sourceFileCount);
			const auto stalledSeconds = std::chrono::duration_cast<std::chrono::seconds>(
				now - m_lastWatchdogProgressTime)
											 .count();
			LOG_WARNING_STREAM(
				<< "indexing watchdog: no progress for " << stalledSeconds
				<< "s. indexed_source_file_count=" << currentIndexedSourceFileCount
				<< "/" << sourceFileCount << ", indexing_file_count="
				<< currentIndexingFileCount << ", running_threads=" << runningThreadCount);
			if (m_lastKnownIndexingFiles.empty())
				LOG_WARNING_STREAM(<< "indexing watchdog: no last known indexing files.");
			for (const FilePath& path: m_lastKnownIndexingFiles)
				LOG_WARNING_STREAM(
					<< "indexing watchdog: last known indexing file: " << path.str());
			m_lastWatchdogLogTime = now;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(50));

		return STATE_RUNNING;
	}
	catch (const std::exception& e)
	{
		LOG_ERROR_STREAM(<< "TaskBuildIndex::doUpdate exception: " << e.what());
		m_interrupted = true;
		m_indexerCommandQueueStopped = true;
		blackboard->set("interrupted_indexing", true);
		utility::killRunningProcesses();
		return STATE_FAILURE;
	}
}

void TaskBuildIndex::doExit(std::shared_ptr<Blackboard> blackboard)
{
	using enum Task::TaskState;
	using enum IndexerCommandType;
	for (auto *processThread: m_processThreads)
	{
		processThread->join();
		delete processThread;
	}
	m_processThreads.clear();

	if (m_interrupted)
	{
		blackboard->set<bool>("indexer_threads_stopped", true);
		return;
	}

	while (fetchIntermediateStorages(blackboard))
		;

	std::vector<FilePath> crashedFiles =
		m_interprocessIndexingStatusManager.getCrashedSourceFilePaths();
	if (!crashedFiles.empty())
	{
		std::shared_ptr<IntermediateStorage> storage = std::make_shared<IntermediateStorage>();
		std::shared_ptr<ParserClientImpl> parserClient = std::make_shared<ParserClientImpl>(storage);

		for (const FilePath& path: crashedFiles)
		{
			Id fileId = parserClient->recordFile(path.getCanonical(), false);
			parserClient->recordError(
				"The indexer subprocess terminated unexpectedly while indexing this translation unit. "
				"Please verify compile flags, language standard, and include paths for this source "
				"file.",
				true,
				true,
				path,
				ParseLocation(fileId, 1, 1));
			LOG_INFO("crashed translation unit: " + path.str());
		}
		m_storageProvider->insert(storage);
	}

	blackboard->set<bool>("indexer_threads_stopped", true);
}

void TaskBuildIndex::doReset(std::shared_ptr<Blackboard>  /*blackboard*/) {}

void TaskBuildIndex::terminate()
{
	m_interrupted = true;
	utility::killRunningProcesses();
}

void TaskBuildIndex::handleMessage(MessageIndexingInterrupted*  /*message*/)
{
	using enum Task::TaskState;
	using enum IndexerCommandType;
	LOG_INFO("sending indexer interrupt command.");

	m_interprocessIndexingStatusManager.setIndexingInterrupted(true);
	m_interrupted = true;
	LOG_INFO("interrupt requested: killing running indexer subprocesses (debug fallback)");
	utility::killRunningProcesses();

	m_dialogView->showUnknownProgressDialog(
		"Interrupting Indexing", "Waiting for indexer\nthreads to finish");
}

void TaskBuildIndex::runIndexerProcess(ProcessId processId, const std::string& logFilePath)
{
	using enum Task::TaskState;
	using enum IndexerCommandType;
	[[maybe_unused]]
	ScopedFunctor runningThreadCounter([&]() { m_runningThreadCount--; });

	const FilePath indexerProcessPath = AppPath::getCxxIndexerFilePath();
	if (!indexerProcessPath.exists())
	{
		m_interrupted = true;
		const IndexerProcessError error = utility::makeExpectedError(
			IndexerProcessErrorCode::ExecutableMissing,
			"Cannot start CXX indexer process because executable is missing at \"" +
				indexerProcessPath.str() + "\".");
		LOG_ERROR_STREAM(<< error);
		return;
	}

	std::vector<std::string> commandArguments;
	commandArguments.push_back(to_string(processId));
	commandArguments.push_back(m_appUUID);
	commandArguments.push_back(AppPath::getSharedDataDirectoryPath().getAbsolute().str());
	commandArguments.push_back(UserPaths::getUserDataDirectoryPath().getAbsolute().str());

	if (!logFilePath.empty())
	{
		commandArguments.push_back(logFilePath);
	}

	const std::string commandLine = buildProcessCommandLine(indexerProcessPath, commandArguments);

	LOG_INFO_STREAM(
		<< "CXX indexer process " << processId << " configured: executable='"
		<< indexerProcessPath.str() << "', args=" << commandArguments.size());

	int result = 1;
	size_t launchAttempt = 0;
	size_t consecutiveFailureCount = 0;
	const size_t maxConsecutiveFailures = 200;
	while ((!m_indexerCommandQueueStopped || result != 0) && !m_interrupted)
	{
		launchAttempt++;
		LOG_INFO_STREAM(
			<< "Launching CXX indexer process " << processId << " attempt " << launchAttempt
			<< " (queueStopped=" << m_indexerCommandQueueStopped
			<< ", interrupted=" << m_interrupted << ")");

		const utility::ProcessOutput processOutput = utility::executeProcess(
			indexerProcessPath.str(), commandArguments, FilePath(), false, INFINITE_TIMEOUT);
		result = processOutput.exitCode;

		LOG_INFO_STREAM(
			<< "CXX indexer process " << processId << " attempt " << launchAttempt
			<< " returned with " + std::to_string(result));

		const IndexerProcessResult processResult = validateIndexerProcessOutput(
			processOutput,
			processId,
			launchAttempt,
			"CXX indexer process",
			commandLine);
		if (processResult)
		{
			consecutiveFailureCount = 0;
			continue;
		}

		if (m_interrupted)
			break;

		LOG_ERROR_STREAM(<< processResult.error());
		logCxxIndexerFailureContext(m_interprocessIndexingStatusManager, processId);

		consecutiveFailureCount++;
		if (consecutiveFailureCount >= maxConsecutiveFailures)
		{
			LOG_ERROR_STREAM(
				<< "CXX indexer process " << processId << " attempt " << launchAttempt
				<< " reached " << consecutiveFailureCount
				<< " consecutive failures. interrupting indexing.");
			m_interrupted = true;
			m_indexerCommandQueueStopped = true;
			m_interprocessIndexingStatusManager.setIndexingInterrupted(true);
			m_interprocessIndexingStatusManager.setQueueStopped(true);
			utility::killRunningProcesses();
			break;
		}

		LOG_WARNING_STREAM(
			<< "CXX indexer process " << processId << " failure " << consecutiveFailureCount
			<< "/" << maxConsecutiveFailures
			<< ". restarting process to continue indexing.");
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}
}

void TaskBuildIndex::runRustIndexerProcess(ProcessId processId, const std::string& logFilePath)
{
	using enum Task::TaskState;
	using enum IndexerCommandType;
	[[maybe_unused]]
	ScopedFunctor runningThreadCounter([&]() { m_runningThreadCount--; });
	try
	{

	const FilePath rustIndexerPath = AppPath::getRustIndexerFilePath();
	if (!rustIndexerPath.exists())
	{
		LOG_WARNING(
			"Rust indexer not found at \"" + rustIndexerPath.str() +
			"\" — Rust files will not be indexed");
		return;
	}

	std::vector<std::string> args;
	args.push_back(to_string(processId));
	args.push_back(m_appUUID);
	args.push_back(AppPath::getSharedDataDirectoryPath().getAbsolute().str());
	args.push_back(UserPaths::getUserDataDirectoryPath().getAbsolute().str());
	if (!logFilePath.empty())
		args.push_back(logFilePath);
	const std::string commandLine = buildProcessCommandLine(rustIndexerPath, args);

	int result = 0;
	size_t launchAttempt = 0;
	size_t consecutiveFailureCount = 0;
	const size_t maxConsecutiveFailures = 200;
	IndexerCommandManagerImpl commandManager(m_appUUID, ProcessId::NONE, false);
	while (!m_interrupted)
	{
		const bool hasRustCommands =
			commandManager.hasIndexerCommandType(INDEXER_COMMAND_RUST);
		if (!hasRustCommands)
		{
			if (m_indexerCommandQueueStopped)
				break;

			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			continue;
		}

		launchAttempt++;
		const utility::ProcessOutput processOutput = utility::executeProcess(
			rustIndexerPath.str(), args, FilePath(), false, INFINITE_TIMEOUT);
		result = processOutput.exitCode;
		LOG_INFO_STREAM(
			<< "Rust indexer process " << processId << " attempt " << launchAttempt
			<< " returned with " << result);
		LOG_INFO_STREAM(
			<< "Rust indexer process " << processId << " attempt " << launchAttempt
			<< " stdout:\n" << processOutput.output);
		LOG_INFO_STREAM(
			<< "Rust indexer process " << processId << " attempt " << launchAttempt
			<< " stderr:\n" << processOutput.error);

		const IndexerProcessResult processResult = validateIndexerProcessOutput(
			processOutput,
			processId,
			launchAttempt,
			"Rust indexer process",
			commandLine);
		if (processResult)
		{
			consecutiveFailureCount = 0;
			if (!m_indexerCommandQueueStopped && !m_interrupted)
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
			continue;
		}

		if (m_interrupted)
			break;

		LOG_ERROR_STREAM(<< processResult.error());

		consecutiveFailureCount++;
		if (consecutiveFailureCount >= maxConsecutiveFailures)
		{
			LOG_ERROR_STREAM(
				<< "Rust indexer process " << processId << " attempt " << launchAttempt
				<< " reached " << consecutiveFailureCount
				<< " consecutive failures. interrupting indexing.");
			m_interrupted = true;
			m_indexerCommandQueueStopped = true;
			m_interprocessIndexingStatusManager.setIndexingInterrupted(true);
			m_interprocessIndexingStatusManager.setQueueStopped(true);
			utility::killRunningProcesses();
			break;
		}

		LOG_WARNING_STREAM(
			<< "Rust indexer process " << processId << " failure " << consecutiveFailureCount
			<< "/" << maxConsecutiveFailures
			<< ". restarting process to continue indexing.");
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}
	}
	catch (const std::exception& e)
	{
		LOG_ERROR_STREAM(
			<< "TaskBuildIndex::runRustIndexerProcess exception: " << e.what());
		m_interrupted = true;
		m_indexerCommandQueueStopped = true;
		utility::killRunningProcesses();
	}
}

void TaskBuildIndex::runSwiftIndexerProcess(ProcessId processId, const std::string& logFilePath)
{
	using enum Task::TaskState;
	using enum IndexerCommandType;
	[[maybe_unused]]
	ScopedFunctor runningThreadCounter([&]() { m_runningThreadCount--; });

	const FilePath swiftIndexerPath = AppPath::getSwiftIndexerFilePath();
	if (!swiftIndexerPath.exists())
	{
		LOG_WARNING(
			"Swift indexer not found at \"" + swiftIndexerPath.str() +
			"\" — Swift files will not be indexed");
		return;
	}

	std::vector<std::string> args;
	args.push_back(to_string(processId));
	args.push_back(m_appUUID);
	args.push_back(AppPath::getSharedDataDirectoryPath().getAbsolute().str());
	args.push_back(UserPaths::getUserDataDirectoryPath().getAbsolute().str());
	if (!logFilePath.empty())
		args.push_back(logFilePath);
	const std::string commandLine = buildProcessCommandLine(swiftIndexerPath, args);

	int result = 0;
	size_t launchAttempt = 0;
	IndexerCommandManagerImpl commandManager(m_appUUID, ProcessId::NONE, false);
	while (!m_interrupted)
	{
		const bool hasSwiftCommands =
			commandManager.hasIndexerCommandType(INDEXER_COMMAND_SWIFT);
		if (!hasSwiftCommands)
		{
			if (m_indexerCommandQueueStopped)
				break;

			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			continue;
		}

		launchAttempt++;
		const utility::ProcessOutput processOutput = utility::executeProcess(
			swiftIndexerPath.str(), args, FilePath(), false, INFINITE_TIMEOUT);
		result = processOutput.exitCode;
		LOG_INFO_STREAM(
			<< "Swift indexer process " << processId << " attempt " << launchAttempt
			<< " returned with " << result);

		const IndexerProcessResult processResult = validateIndexerProcessOutput(
			processOutput,
			processId,
			launchAttempt,
			"Swift indexer process",
			commandLine);
		if (!processResult)
		{
			LOG_ERROR_STREAM(
				<< processResult.error() << " interrupting indexing.");
			m_interrupted = true;
			m_indexerCommandQueueStopped = true;
			m_interprocessIndexingStatusManager.setIndexingInterrupted(true);
			m_interprocessIndexingStatusManager.setQueueStopped(true);
			utility::killRunningProcesses();
			break;
		}

		if (!m_indexerCommandQueueStopped && !m_interrupted)
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}
}

bool TaskBuildIndex::fetchIntermediateStorages(std::shared_ptr<Blackboard> blackboard)
{
	using enum Task::TaskState;
	using enum IndexerCommandType;
	// Always drain IPC shared memory segments into a local queue first.
	// This prevents the subprocess back-pressure check (storageCount < 2) from
	// deadlocking when the provider queue is full — IPC must be drained regardless.
	std::vector<std::shared_ptr<IntermediateStorage>> drained;

	// Drain via finished-process signals first.
	TimeStamp t = TimeStamp::now();
	while (TimeStamp::now().deltaMS(t) < 500)
	{
		ProcessId finishedProcessId = m_interprocessIndexingStatusManager.getNextFinishedProcessId();
		if (finishedProcessId == ProcessId::NONE)
			break;

		std::shared_ptr<IntermediateStorageManagerImpl> storageManager;
		if constexpr (hasSwiftLanguagePackage)
		{
			const ProcessId swiftProcessId{swiftIndexerProcessId(m_processCount)};
			if (finishedProcessId == swiftProcessId && m_swiftStorageManager)
				storageManager = m_swiftStorageManager;
		}
		if constexpr (hasRustLanguagePackage)
		{
			const ProcessId rustProcessId{rustIndexerProcessId(m_processCount)};
			if (!storageManager && finishedProcessId == rustProcessId && m_rustStorageManager)
				storageManager = m_rustStorageManager;
		}
		if (
			!storageManager &&
			static_cast<size_t>(finishedProcessId) <= m_interprocessIntermediateStorageManagers.size())
			storageManager =
				m_interprocessIntermediateStorageManagers[static_cast<size_t>(finishedProcessId) - 1];

		if (!storageManager)
			continue;

		if (storageManager->peekCount() == 0)
			continue;

		LOG_INFO_STREAM(<< storageManager->getProcessId() << " - storage count: " << storageManager->peekCount());
		drained.push_back(storageManager->popIntermediateStorage());
	}

	// Also scan all managers directly — guards against missed signals.
	// No time gate here: we must drain even if the signal loop took >500ms.
	{
		for (auto& mgr: m_interprocessIntermediateStorageManagers)
		{
			if (!mgr || mgr->peekCount() == 0)
				continue;
			LOG_INFO_STREAM(<< mgr->getProcessId() << " - unsignaled storage count: " << mgr->peekCount());
			drained.push_back(mgr->popIntermediateStorage());
		}
		if constexpr (hasRustLanguagePackage)
		{
			if (m_rustStorageManager && m_rustStorageManager->peekCount() > 0)
			{
				LOG_INFO_STREAM(<< m_rustStorageManager->getProcessId() << " - unsignaled rust storage");
				drained.push_back(m_rustStorageManager->popIntermediateStorage());
			}
		}
		if constexpr (hasSwiftLanguagePackage)
		{
			if (m_swiftStorageManager && m_swiftStorageManager->peekCount() > 0)
			{
				LOG_INFO_STREAM(<< m_swiftStorageManager->getProcessId() << " - unsignaled swift storage");
				drained.push_back(m_swiftStorageManager->popIntermediateStorage());
			}
		}
	}

	if (drained.empty())
		return false;

	// Now insert drained storages into the provider, throttling if it's full.
	const int maxQueuedStoragesBeforePause = 30;
	for (auto& storage: drained)
	{
		while (m_storageProvider->getStorageCount() > maxQueuedStoragesBeforePause)
			std::this_thread::sleep_for(std::chrono::milliseconds(50));

		m_storageProvider->insert(storage);
	}

	blackboard->update<int>(
		"indexed_source_file_count",
		[n = static_cast<int>(drained.size())](int count) { return count + n; });

	return true;
}

void TaskBuildIndex::updateIndexingDialog(std::shared_ptr<Blackboard> blackboard, const std::vector<FilePath>& sourcePaths)
{
	// TODO: factor in unindexed files...
	int sourceFileCount = 0;
	int indexedSourceFileCount = 0;
	blackboard->get("source_file_count", sourceFileCount);
	blackboard->get("indexed_source_file_count", indexedSourceFileCount);

	m_indexingFileCount += sourcePaths.size();

	m_dialogView->updateIndexingDialog(m_indexingFileCount, indexedSourceFileCount, sourceFileCount, sourcePaths);

	int progress = 0;
	if (sourceFileCount != 0)
	{
		progress = indexedSourceFileCount * 100 / sourceFileCount;
	}
	MessageIndexingStatus(true, progress).dispatch();
}
