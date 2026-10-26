#include "TaskBuildIndex.h"

#include <stdexcept>

#include "language_package_flags.h"
#include "AppPath.h"
#include "IndexerCommandType.h"
#include "DialogView.h"
#include "IndexingRunState.h"
#include "FileLogger.h"
#include "MessageIndexingStatus.h"
#include "ParserClientImpl.h"
#include "ScopedFunctor.h"
#include "StorageProvider.h"
#include "TimeStamp.h"
#include "UserPaths.h"
#include "WorkPools.h"
#include "utilityApp.h"
#include "utilityExpected.h"

using namespace utility;

namespace
{
	using enum IndexerCommandType;
constexpr bool hasRustLanguagePackage{language_packages::buildRustLanguagePackage};
constexpr bool hasSwiftLanguagePackage{language_packages::buildSwiftLanguagePackage};
constexpr bool hasZigLanguagePackage{language_packages::buildZigLanguagePackage};

// Rust supervisors take the ids right after the C++ range (crate fan-out R1):
// processCount+1 .. processCount+rustSupervisorCount; the Swift supervisors
// (SW6) follow them: processCount+rustSupervisorCount+1 .. +swiftSupervisorCount.
[[maybe_unused]] ProcessId rustIndexerProcessId(const size_t processCount, const size_t supervisorIndex)
{
	return static_cast<ProcessId>(processCount + 1 + supervisorIndex);
}

[[maybe_unused]] ProcessId swiftIndexerProcessId(
	const size_t processCount, const size_t rustSupervisorCount, const size_t supervisorIndex)
{
	if constexpr (hasRustLanguagePackage)
		return static_cast<ProcessId>(processCount + rustSupervisorCount + 1 + supervisorIndex);

	return static_cast<ProcessId>(processCount + 1 + supervisorIndex);
}

// Zig supervisors take the ids trailing the Swift range: processCount +
// (rust supervisors if enabled) + (swift supervisors if enabled) + 1 + index.
[[maybe_unused]] ProcessId zigIndexerProcessId(
	const size_t processCount,
	const size_t rustSupervisorCount,
	const size_t swiftSupervisorCount,
	const size_t supervisorIndex)
{
	size_t base = processCount;
	if constexpr (hasRustLanguagePackage)
		base += rustSupervisorCount;
	if constexpr (hasSwiftLanguagePackage)
		base += swiftSupervisorCount;
	return static_cast<ProcessId>(base + 1 + supervisorIndex);
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
	const std::string& appUUID,
	const std::vector<IndexerClusterEntry>& cxxClusterPlan,
	size_t rustSupervisorCount,
	size_t swiftSupervisorCount,
	size_t zigSupervisorCount)
	: m_storageProvider(storageProvider)
	, m_dialogView(dialogView)
	, m_appUUID(appUUID)
	, m_interprocessIndexingStatusManager(appUUID, ProcessId::NONE, true)
	, m_processCount(processCount)
	, m_rustSupervisorCount(rustSupervisorCount > 0 ? rustSupervisorCount : 1)
	, m_swiftSupervisorCount(swiftSupervisorCount > 0 ? swiftSupervisorCount : 1)
	, m_zigSupervisorCount(zigSupervisorCount > 0 ? zigSupervisorCount : 1)
{
	if (m_rustSupervisorCount > 1)
	{
		LOG_INFO_STREAM(
			<< "rust crate fan-out: " << m_rustSupervisorCount << " supervisors");
	}
	if (m_swiftSupervisorCount > 1)
	{
		LOG_INFO_STREAM(
			<< "swift package fan-out: " << m_swiftSupervisorCount << " supervisors");
	}
	if (m_zigSupervisorCount > 1)
	{
		LOG_INFO_STREAM(
			<< "zig fan-out: " << m_zigSupervisorCount << " supervisors");
	}
	if (cxxClusterPlan.empty())
		return;

	// Fan-out S3: sequential ProcessIds cluster by cluster, each pinned to its
	// source group. The Rust/Swift supervisor ids trail m_processCount as usual.
	m_processCount = 0;
	for (const IndexerClusterEntry& cluster: cxxClusterPlan)
	{
		for (size_t k = 0; k < cluster.subprocessCount; k++)
		{
			m_processCount++;
			m_processGroupIds[static_cast<ProcessId>(m_processCount)] = cluster.sourceGroupId;
		}
		LOG_INFO_STREAM(
			<< "indexer cluster plan: source group " << cluster.sourceGroupId << " ("
			<< languageTypeToString(cluster.language) << ", " << cluster.commandCount
			<< " commands) -> " << cluster.subprocessCount << " subprocess(es)");
	}
}

TaskBuildIndex::~TaskBuildIndex()
{
	// Safety net for abrupt teardown (destroyed without a clean end()): stop the
	// supervisors and kill any live subprocesses, so they end before what they use.
	if (m_runningSupervisorCount > 0)
	{
		m_stopSource.request_stop();
		killProcesses();
		if (!waitForSupervisors(std::chrono::seconds(10)))
		{
			LOG_ERROR("indexer supervisors still running as their task goes");
		}
	}
	m_processes.reset();
	// m_stopCallback unregisters (declared last).
}

exec::task<void> TaskBuildIndex::run(IndexingRunState& state)
{
	start(state);
	while (true)
	{
		const Look look = this->look(state);
		if (look == Look::Running)
		{
			co_await execution::delay(std::chrono::milliseconds(50));	// holding no thread
			continue;
		}
		end(state);
		if (look == Look::Failed)
		{
			throw std::runtime_error("building the index failed; see the errors above");
		}
		co_return;
	}
}

void TaskBuildIndex::start(IndexingRunState& state)
{
	m_interprocessIndexingStatusManager.setIndexingInterrupted(false);
	m_interprocessIndexingStatusManager.setQueueStopped(false);

	m_indexingFileCount = 0;
	m_lastWatchdogIndexedSourceFileCount = 0;
	m_lastWatchdogIndexingFileCount = 0;
	m_lastKnownIndexingFiles.clear();
	m_lastWatchdogProgressTime = std::chrono::steady_clock::now();
	m_lastWatchdogLogTime = m_lastWatchdogProgressTime;
	m_indexingStartTime = m_lastWatchdogProgressTime;
	updateIndexingDialog(state, std::vector<FilePath>());

	std::string logFilePath;
	Logger* logger = LogManager::getInstance()->getLoggerByType("FileLogger");
	if (logger != nullptr)
	{
		logFilePath = dynamic_cast<FileLogger*>(logger)->getLogFilePath().string();
	}

	// When indexing is interrupted (request_stop), raise the IPC interrupt flag so the
	// subprocesses stop gracefully; the babysitter loops observe m_stopSource directly.
	m_stopCallback.emplace(m_stopSource.get_token(), StopCallback{ &m_interprocessIndexingStatusManager });

	if (!m_processes)
	{
		m_processes = std::make_unique<ProcessMonitor>();
	}

	// start indexer processes
	for (size_t i = 0; i < m_processCount; i++)
	{
		const ProcessId processId = static_cast<ProcessId>(i + 1);	// 0 remains reserved for the main process

		m_interprocessIntermediateStorageManagers.push_back(std::make_shared<IntermediateStorageManagerImpl>(m_appUUID, processId, true));
		startSupervisor(cxxSupervisor(processId, logFilePath));
	}

	// Start the Rust indexer supervisors (crate fan-out R1): each babysits one
	// subprocess per crate command, sharing the queue via accept-any pops. They
	// get the ids after the CXX indexers so each has its own storage channel.
	if constexpr (hasRustLanguagePackage)
	{
		for (size_t k = 0; k < m_rustSupervisorCount; k++)
		{
			const ProcessId rustProcessId{rustIndexerProcessId(m_processCount, k)};
			m_rustStorageManagers.push_back(std::make_shared<IntermediateStorageManagerImpl>(
				m_appUUID, rustProcessId, true));
			startSupervisor(externalSupervisor(
				rustProcessId,
				logFilePath,
				AppPath::getRustIndexerFilePath(),
				IndexerCommandType::INDEXER_COMMAND_RUST,
				"Rust indexer process"));
		}
	}

	// Start the Swift indexer supervisors (SW6): the analog of the Rust loop,
	// with ids following the Rust supervisors so each has its own storage
	// channel.
	if constexpr (hasSwiftLanguagePackage)
	{
		for (size_t k = 0; k < m_swiftSupervisorCount; k++)
		{
			const ProcessId swiftProcessId{
				swiftIndexerProcessId(m_processCount, m_rustSupervisorCount, k)};
			m_swiftStorageManagers.push_back(std::make_shared<IntermediateStorageManagerImpl>(
				m_appUUID, swiftProcessId, true));
			startSupervisor(externalSupervisor(
				swiftProcessId,
				logFilePath,
				AppPath::getSwiftIndexerFilePath(),
				IndexerCommandType::INDEXER_COMMAND_SWIFT,
				"Swift indexer process"));
		}
	}

	// Start the Zig indexer supervisors: the analog of the Rust/Swift loops,
	// with ids trailing the Swift supervisors so each has its own storage
	// channel.
	if constexpr (hasZigLanguagePackage)
	{
		for (size_t k = 0; k < m_zigSupervisorCount; k++)
		{
			const ProcessId zigProcessId{
				zigIndexerProcessId(m_processCount, m_rustSupervisorCount, m_swiftSupervisorCount, k)};
			m_zigStorageManagers.push_back(std::make_shared<IntermediateStorageManagerImpl>(
				m_appUUID, zigProcessId, true));
			startSupervisor(externalSupervisor(
				zigProcessId,
				logFilePath,
				AppPath::getZigIndexerFilePath(),
				IndexerCommandType::INDEXER_COMMAND_ZIG,
				"Zig indexer process"));
		}
	}

	state.begin(state.indexersStarted);
}

TaskBuildIndex::Look TaskBuildIndex::look(IndexingRunState& state)
{
	using enum IndexerCommandType;
	try
	{
		size_t runningThreadCount = m_runningSupervisorCount;

		const bool queueStopped = m_indexerCommandQueueStopped.load() || state.commandQueueStopped.load();
		m_indexerCommandQueueStopped.store(queueStopped);
		if (queueStopped && !m_interprocessIndexingStatusManager.getQueueStopped())
			m_interprocessIndexingStatusManager.setQueueStopped(true);

		const std::vector<FilePath> indexingFiles = m_interprocessIndexingStatusManager.getCurrentlyIndexedSourceFilePaths();
		if (!indexingFiles.empty())
		{
			m_lastKnownIndexingFiles = indexingFiles;
			updateIndexingDialog(state, indexingFiles);
		}

		if (queueStopped && runningThreadCount == 0)
		{
			LOG_INFO_STREAM(<< "command queue stopped and no running threads. done.");
			return Look::Done;
		}
		else if (m_stopSource.stop_requested())
		{
			LOG_INFO_STREAM(<< "interrupted indexing.");
			state.interrupted = true;
			return Look::Done;
		}

		if (fetchIntermediateStorages(state))
		{
			updateIndexingDialog(state, std::vector<FilePath>());
		}

		const int indexedSourceFileCount = state.indexedSourceFileCount;
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
			!queueStopped &&
			!m_stopSource.stop_requested() &&
			runningThreadCount > 0 &&
			now - m_lastWatchdogProgressTime > std::chrono::seconds(30) &&
			now - m_lastWatchdogLogTime > std::chrono::seconds(10))
		{
			const int sourceFileCount = state.sourceFileCount;
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

		return Look::Running;
	}
	catch (const std::exception& e)
	{
		LOG_ERROR_STREAM(<< "TaskBuildIndex::look exception: " << e.what());
		m_stopSource.request_stop();
		m_indexerCommandQueueStopped.store(true);
		state.interrupted = true;
		killProcesses();
		return Look::Failed;
	}
}

void TaskBuildIndex::logIndexingSummary(const IndexingRunState& state) const
{
	// B2-gate numbers: indexing wall time vs writer back-pressure stalls. The
	// storage writer / pre-merge log their busy totals separately (TaskInjectStorage
	// / TaskMergeStorages summaries).
	const auto wallMs = std::chrono::duration_cast<std::chrono::milliseconds>(
							std::chrono::steady_clock::now() - m_indexingStartTime)
							.count();

	LOG_INFO_STREAM(
		<< "indexing summary: wall " << wallMs << " ms, " << state.indexedSourceFileCount.load()
		<< " source files, writer back-pressure stalls " << m_throttleStallCount << " ("
		<< m_throttleStallMs << " ms)");
}

void TaskBuildIndex::end(IndexingRunState& state)
{
	using enum IndexerCommandType;
	// Cooperative teardown: on interruption the supervisors are already stopping
	// (m_stopSource + the IPC interrupt flag). Give the subprocesses a brief window to
	// exit gracefully, then kill any stragglers. On normal completion the supervisors
	// have all ended already -> no wait.
	if (m_stopSource.stop_requested() && !waitForSupervisors(std::chrono::seconds(3)))
	{
		LOG_WARNING_STREAM(
			<< "indexer interrupt: " << m_runningSupervisorCount.load()
			<< " subprocess supervisor(s) still running after 3s; killing.");
		killProcesses();
	}
	// Their subprocesses are done or killed: the supervisors end promptly.
	if (!waitForSupervisors(std::chrono::seconds(10)))
	{
		LOG_WARNING_STREAM(
			<< "indexer teardown: " << m_runningSupervisorCount.load()
			<< " subprocess supervisor(s) still running after 10s.");
	}

	if (m_stopSource.stop_requested())
	{
		state.indexersStopped = true;
		m_storageProvider->setDone();	 // release the merge/inject waiters
		logIndexingSummary(state);
		return;
	}

	while (fetchIntermediateStorages(state))
		;
	handOnStorages(state, true);	// the producers are done: the writer takes the rest

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

	state.indexersStopped = true;
	m_storageProvider->setDone();	 // release the merge/inject waiters
	logIndexingSummary(state);
}

void TaskBuildIndex::handleMessage(MessageIndexingInterrupted*  /*message*/)
{
	LOG_INFO("indexer interrupt requested; stopping subprocesses cooperatively.");

	// request_stop() fires the stop callback, which raises the IPC interrupt flag so
	// subprocesses finish their current TU and exit; the babysitter loops stop
	// relaunching. Stragglers are killed by the bounded fallback in end().
	m_stopSource.request_stop();

	m_dialogView->showUnknownProgressDialog(
		"Interrupting Indexing", "Waiting for indexer\nthreads to finish");
}

struct TaskBuildIndex::Supervisor
{
	ProcessId processId = ProcessId::NONE;
	FilePath executable;
	std::vector<std::string> arguments;
	std::string commandLine;
	std::string label;	// "CXX indexer process", "Rust indexer process", ...
	// An external indexer's: the commands it waits for, and the queue it looks at.
	std::optional<IndexerCommandType> commandType;
	std::unique_ptr<IndexerCommandManagerImpl> commands;

	size_t launchAttempt = 0;
	size_t consecutiveFailureCount = 0;
	int result = 1;	// the last exit code
	bool finished = false;	// on the monitor's thread
};

namespace
{
constexpr size_t maxConsecutiveFailures = 200;
constexpr std::chrono::milliseconds relaunchDelay {200};
}	 // namespace

std::shared_ptr<TaskBuildIndex::Supervisor> TaskBuildIndex::cxxSupervisor(
	ProcessId processId, const std::string& logFilePath)
{
	const FilePath indexerProcessPath = AppPath::getCxxIndexerFilePath();
	if (!indexerProcessPath.exists())
	{
		m_stopSource.request_stop();
		const IndexerProcessError error = utility::makeExpectedError(
			IndexerProcessErrorCode::ExecutableMissing,
			"Cannot start CXX indexer process because executable is missing at \"" +
				indexerProcessPath.str() + "\".");
		LOG_ERROR_STREAM(<< error);
		return nullptr;
	}

	auto supervisor = std::make_shared<Supervisor>();
	supervisor->processId = processId;
	supervisor->executable = indexerProcessPath;
	supervisor->label = "CXX indexer process";
	std::vector<std::string>& commandArguments = supervisor->arguments;
	commandArguments.push_back(to_string(processId));
	commandArguments.push_back(m_appUUID);
	commandArguments.push_back(AppPath::getSharedDataDirectoryPath().getAbsolute().str());
	commandArguments.push_back(UserPaths::getUserDataDirectoryPath().getAbsolute().str());

	if (!logFilePath.empty())
	{
		commandArguments.push_back(logFilePath);
	}

	// Fan-out S2: pin this subprocess to its cluster's source group. The flag is
	// parsed out before the positional arguments in the indexer's main().
	if (const auto groupIt = m_processGroupIds.find(processId);
		groupIt != m_processGroupIds.end() && !groupIt->second.empty())
	{
		commandArguments.push_back("--only-group-id=" + groupIt->second);
	}

	supervisor->commandLine = buildProcessCommandLine(indexerProcessPath, commandArguments);

	LOG_INFO_STREAM(
		<< "CXX indexer process " << processId << " configured: executable='"
		<< indexerProcessPath.str() << "', args=" << commandArguments.size());
	return supervisor;
}

std::shared_ptr<TaskBuildIndex::Supervisor> TaskBuildIndex::externalSupervisor(
	ProcessId processId,
	const std::string& logFilePath,
	const FilePath& indexerPath,
	IndexerCommandType commandType,
	const std::string& humanName)
{
	if (!indexerPath.exists())
	{
		LOG_WARNING(
			humanName + " not found at \"" + indexerPath.str() +
			"\" — files of this language will not be indexed");
		return nullptr;
	}

	auto supervisor = std::make_shared<Supervisor>();
	supervisor->processId = processId;
	supervisor->executable = indexerPath;
	supervisor->label = humanName;
	supervisor->commandType = commandType;
	supervisor->commands = std::make_unique<IndexerCommandManagerImpl>(m_appUUID, ProcessId::NONE, false);
	std::vector<std::string>& args = supervisor->arguments;
	args.push_back(to_string(processId));
	args.push_back(m_appUUID);
	args.push_back(AppPath::getSharedDataDirectoryPath().getAbsolute().str());
	args.push_back(UserPaths::getUserDataDirectoryPath().getAbsolute().str());
	if (!logFilePath.empty())
		args.push_back(logFilePath);
	supervisor->commandLine = buildProcessCommandLine(indexerPath, args);
	return supervisor;
}

void TaskBuildIndex::startSupervisor(std::shared_ptr<Supervisor> supervisor)
{
	if (!supervisor)
	{
		return;	// its indexer is missing
	}
	{
		const std::lock_guard<std::mutex> lock(m_supervisorsMutex);
		m_runningSupervisorCount++;
	}
	m_processes->post([this, supervisor = std::move(supervisor)]() { launch(supervisor); });
}

void TaskBuildIndex::launch(const std::shared_ptr<Supervisor>& supervisor)
{
	using enum IndexerCommandType;
	if (supervisor->finished)
	{
		return;
	}
	try
	{
		if (m_stopSource.stop_requested())
		{
			finish(*supervisor);
			return;
		}

		if (!supervisor->commandType)
		{
			// While the command queue runs, or until the last run succeeded.
			if (m_indexerCommandQueueStopped.load() && supervisor->result == 0)
			{
				finish(*supervisor);
				return;
			}
			supervisor->launchAttempt++;
			LOG_INFO_STREAM(
				<< "Launching CXX indexer process " << supervisor->processId << " attempt "
				<< supervisor->launchAttempt << " (queueStopped=" << m_indexerCommandQueueStopped.load()
				<< ", interrupted=" << m_stopSource.stop_requested() << ")");
		}
		else
		{
			// While commands of its type remain.
			if (!supervisor->commands->hasIndexerCommandType(*supervisor->commandType))
			{
				if (m_indexerCommandQueueStopped.load())
				{
					finish(*supervisor);
					return;
				}
				again(supervisor, relaunchDelay);
				return;
			}
			supervisor->launchAttempt++;
		}

		m_processes->start(
			supervisor->executable.str(), supervisor->arguments, [this, supervisor](utility::ProcessOutput output) {
				ended(supervisor, output);
			});
	}
	catch (const std::exception& e)
	{
		LOG_ERROR_STREAM(<< "TaskBuildIndex::launch (" << supervisor->label << ") exception: " << e.what());
		m_stopSource.request_stop();
		m_indexerCommandQueueStopped.store(true);
		killProcesses();
		finish(*supervisor);
	}
}

void TaskBuildIndex::ended(const std::shared_ptr<Supervisor>& supervisor, const utility::ProcessOutput& output)
{
	if (supervisor->finished)
	{
		return;
	}
	try
	{
		const bool external = supervisor->commandType.has_value();
		supervisor->result = output.exitCode;
		LOG_INFO_STREAM(
			<< supervisor->label << " " << supervisor->processId << " attempt " << supervisor->launchAttempt
			<< " returned with " << output.exitCode);
		if (external)
		{
			LOG_INFO_STREAM(
				<< supervisor->label << " " << supervisor->processId << " attempt "
				<< supervisor->launchAttempt << " stdout:\n" << output.output);
			LOG_INFO_STREAM(
				<< supervisor->label << " " << supervisor->processId << " attempt "
				<< supervisor->launchAttempt << " stderr:\n" << output.error);
		}

		const IndexerProcessResult processResult = validateIndexerProcessOutput(
			output, supervisor->processId, supervisor->launchAttempt, supervisor->label, supervisor->commandLine);
		if (processResult)
		{
			supervisor->consecutiveFailureCount = 0;
			// An external indexer that finished its commands looks again in a while.
			if (external && !m_indexerCommandQueueStopped.load() && !m_stopSource.stop_requested())
			{
				again(supervisor, relaunchDelay);
			}
			else
			{
				launch(supervisor);
			}
			return;
		}

		if (m_stopSource.stop_requested())
		{
			finish(*supervisor);
			return;
		}

		LOG_ERROR_STREAM(<< processResult.error());
		if (!external)
		{
			logCxxIndexerFailureContext(m_interprocessIndexingStatusManager, supervisor->processId);
		}

		supervisor->consecutiveFailureCount++;
		if (supervisor->consecutiveFailureCount >= maxConsecutiveFailures)
		{
			LOG_ERROR_STREAM(
				<< supervisor->label << " " << supervisor->processId << " attempt " << supervisor->launchAttempt
				<< " reached " << supervisor->consecutiveFailureCount
				<< " consecutive failures. interrupting indexing.");
			interruptAll();
			finish(*supervisor);
			return;
		}

		LOG_WARNING_STREAM(
			<< supervisor->label << " " << supervisor->processId << " failure "
			<< supervisor->consecutiveFailureCount << "/" << maxConsecutiveFailures
			<< ". restarting process to continue indexing.");
		again(supervisor, relaunchDelay);
	}
	catch (const std::exception& e)
	{
		LOG_ERROR_STREAM(<< "TaskBuildIndex::ended (" << supervisor->label << ") exception: " << e.what());
		m_stopSource.request_stop();
		m_indexerCommandQueueStopped.store(true);
		killProcesses();
		finish(*supervisor);
	}
}

void TaskBuildIndex::again(const std::shared_ptr<Supervisor>& supervisor, std::chrono::milliseconds delay)
{
	// The supervisor has not ended, so the task and its monitor wait for it.
	execution::afterDelay(delay, [this, supervisor]() {
		m_processes->post([this, supervisor]() { launch(supervisor); });
	});
}

void TaskBuildIndex::finish(Supervisor& supervisor)
{
	if (std::exchange(supervisor.finished, true))
	{
		return;
	}
	{
		const std::lock_guard<std::mutex> lock(m_supervisorsMutex);
		m_runningSupervisorCount--;
	}
	m_supervisorsEnded.notify_all();
}

bool TaskBuildIndex::waitForSupervisors(std::chrono::milliseconds limit)
{
	std::unique_lock<std::mutex> lock(m_supervisorsMutex);
	return m_supervisorsEnded.wait_for(lock, limit, [this]() { return m_runningSupervisorCount == 0; });
}

void TaskBuildIndex::interruptAll()
{
	m_stopSource.request_stop();
	m_indexerCommandQueueStopped.store(true);
	m_interprocessIndexingStatusManager.setIndexingInterrupted(true);
	m_interprocessIndexingStatusManager.setQueueStopped(true);
	killProcesses();
}

void TaskBuildIndex::killProcesses()
{
	if (m_processes)
	{
		m_processes->killAll();
	}
}

bool TaskBuildIndex::fetchIntermediateStorages(IndexingRunState& state)
{
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
			for (const auto& swiftManager: m_swiftStorageManagers)
				{
					if (!storageManager && swiftManager &&
						finishedProcessId == swiftManager->getProcessId())
						storageManager = swiftManager;
				}
		}
		if constexpr (hasRustLanguagePackage)
		{
			for (const auto& rustManager: m_rustStorageManagers)
			{
				if (!storageManager && rustManager &&
					finishedProcessId == rustManager->getProcessId())
					storageManager = rustManager;
			}
		}
		if constexpr (hasZigLanguagePackage)
		{
			for (const auto& zigManager: m_zigStorageManagers)
			{
				if (!storageManager && zigManager &&
					finishedProcessId == zigManager->getProcessId())
					storageManager = zigManager;
			}
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
			for (const auto& rustManager: m_rustStorageManagers)
			{
				if (rustManager && rustManager->peekCount() > 0)
				{
					LOG_INFO_STREAM(<< rustManager->getProcessId() << " - unsignaled rust storage");
					drained.push_back(rustManager->popIntermediateStorage());
				}
			}
		}
		if constexpr (hasSwiftLanguagePackage)
		{
			for (const auto& swiftManager: m_swiftStorageManagers)
				{
					if (swiftManager && swiftManager->peekCount() > 0)
					{
						LOG_INFO_STREAM(<< swiftManager->getProcessId() << " - unsignaled swift storage");
						drained.push_back(swiftManager->popIntermediateStorage());
					}
				}
		}
		if constexpr (hasZigLanguagePackage)
		{
			for (const auto& zigManager: m_zigStorageManagers)
			{
				if (zigManager && zigManager->peekCount() > 0)
				{
					LOG_INFO_STREAM(<< zigManager->getProcessId() << " - unsignaled zig storage");
					drained.push_back(zigManager->popIntermediateStorage());
				}
			}
		}
	}

	// Now hand them to the provider while the writer keeps up. What it cannot take yet
	// waits here for a later update: no thread waits for the writer.
	for (std::shared_ptr<IntermediateStorage>& storage: drained)
	{
		m_pendingStorages.push_back(std::move(storage));
	}
	const size_t handed = handOnStorages(state, false);
	if (!m_pendingStorages.empty())
	{
		// Writer back-pressure: the (serial) storage writer is behind, for an update's
		// rest. Counted as B2-gate evidence for the sharded-ingest decision.
		m_throttleStallCount++;
		m_throttleStallMs += 50;
	}

	return !drained.empty() || handed > 0;
}

size_t TaskBuildIndex::handOnStorages(IndexingRunState& state, bool all)
{
	// As many as the provider holds before the writer is considered behind.
	const int maxQueuedStoragesBeforePause = 30;
	size_t handed = 0;
	while (!m_pendingStorages.empty() &&
		   (all || m_storageProvider->getStorageCount() <= maxQueuedStoragesBeforePause))
	{
		m_storageProvider->insert(std::move(m_pendingStorages.front()));
		m_pendingStorages.pop_front();
		handed++;
	}

	if (handed > 0)
	{
		state.indexedSourceFileCount += static_cast<int>(handed);
	}
	return handed;
}

void TaskBuildIndex::updateIndexingDialog(const IndexingRunState& state, const std::vector<FilePath>& sourcePaths)
{
	// TODO: factor in unindexed files...
	const int sourceFileCount = state.sourceFileCount;
	const int indexedSourceFileCount = state.indexedSourceFileCount;

	m_indexingFileCount += sourcePaths.size();

	m_dialogView->updateIndexingDialog(m_indexingFileCount, indexedSourceFileCount, sourceFileCount, sourcePaths);

	int progress = 0;
	if (sourceFileCount != 0)
	{
		progress = indexedSourceFileCount * 100 / sourceFileCount;
	}
	MessageIndexingStatus(true, progress).dispatch();
}
