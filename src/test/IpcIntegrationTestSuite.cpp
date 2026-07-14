#include "Catch2.hpp"

#include "language_package_flags.h"

#include <chrono>
#include <thread>

#include "AppPath.h"
#include "Blackboard.h"
#include "CombinedIndexerCommandProvider.h"
#include "DialogView.h"
#include "IpcInterprocessIndexerCommandManager.h"
#include "IpcInterprocessIntermediateStorageManager.h"
#include "IpcInterprocessIndexingStatusManager.h"
#include "IpcSharedMemoryGarbageCollector.h"
#include "MemoryIndexerCommandProvider.h"
#include "OptionalCxxTestUtils.h"
#include "OptionalRustTestUtils.h"
#include "OptionalSwiftTestUtils.h"
#include "StorageProvider.h"
#include "TaskBuildIndex.h"
#include "TaskFillIndexerCommandQueue.h"
#include "utilityApp.h"

#include "IntermediateStorage.h"

#include "IndexerCommandSwift.h"

namespace
{
int withOptionalCxxCount(const int& count)
{
	if constexpr (language_packages::buildCxxLanguagePackage)
		return count + 1;
	return count;
}

using MakeLanguageCommandFn = std::shared_ptr<IndexerCommand> (*)(const std::string&);
using IsLanguageCommandFn = bool (*)(const std::shared_ptr<IndexerCommand>&);
using GetLanguageWorkingDirectoryFn = std::string (*)(const std::shared_ptr<IndexerCommand>&);

void assertTaskFillQueueDeduplicatesLanguageCommandsByWorkingDirectory(
	const std::string& dedupUuid,
	const ProcessId& mainPid,
	const ProcessId& workerPid,
	const MakeLanguageCommandFn makeLanguageCommand,
	const IsLanguageCommandFn isLanguageCommand,
	const GetLanguageWorkingDirectoryFn getLanguageWorkingDirectory,
	const std::string& firstWorkingDirectory,
	const std::string& secondWorkingDirectory,
	const std::string& optionalCxxSourcePath)
{
	using enum Task::TaskState;

	IpcInterprocessIndexerCommandManager ownerMgr(dedupUuid, mainPid, true);
	ownerMgr.clearIndexerCommands();

	auto provider = std::make_unique<CombinedIndexerCommandProvider>();

	std::vector<std::shared_ptr<IndexerCommand>> providerACommands;
	providerACommands.push_back(makeLanguageCommand(firstWorkingDirectory));
	if (const std::shared_ptr<IndexerCommand> cxxCommand = makeOptionalCxxCommand(
			optionalCxxSourcePath,
			std::set<FilePath>{},
			"/build",
			std::vector<std::string>{"-std=c++20"});
		cxxCommand)
		providerACommands.push_back(cxxCommand);
	provider->addProvider(
		std::make_shared<MemoryIndexerCommandProvider>(providerACommands), "group-a");

	provider->addProvider(
		std::make_shared<MemoryIndexerCommandProvider>(
			std::vector<std::shared_ptr<IndexerCommand>>{
				makeLanguageCommand(firstWorkingDirectory),
				makeLanguageCommand(secondWorkingDirectory)}),
		"group-b");

	auto blackboard = std::make_shared<Blackboard>();
	blackboard->set<int>("source_file_count", static_cast<int>(provider->size()));

	TaskFillIndexerCommandsQueue task(dedupUuid, std::move(provider), 16);
	REQUIRE(task.update(blackboard) == STATE_SUCCESS);

	int sourceFileCount = 0;
	REQUIRE(blackboard->get<int>("source_file_count", sourceFileCount));
	REQUIRE(sourceFileCount == withOptionalCxxCount(2));

	IpcInterprocessIndexerCommandManager reader(dedupUuid, workerPid, false);

	size_t languageCount = 0;
	size_t cxxCount = 0;
	std::set<std::string> languageWorkingDirectories;
	std::set<std::string> poppedGroupIds;
	while (std::shared_ptr<IndexerCommand> command = reader.popIndexerCommand())
	{
		// The source-group tag (fan-out S1) survives the queue serialization.
		REQUIRE(!command->getSourceGroupId().empty());
		poppedGroupIds.insert(command->getSourceGroupId());

		if (isLanguageCommand(command))
		{
			languageCount++;
			const std::string workingDirectory = getLanguageWorkingDirectory(command);
			REQUIRE(!workingDirectory.empty());
			languageWorkingDirectories.insert(workingDirectory);
			continue;
		}

		if (isOptionalCxxCommand(command))
		{
			cxxCount++;
			REQUIRE(command->getSourceGroupId() == "group-a");  // only group A holds one
		}
	}

	REQUIRE(languageCount == 2);
	REQUIRE(languageWorkingDirectories.size() == 2);
	REQUIRE(cxxCount == static_cast<size_t>(withOptionalCxxCount(0)));
	REQUIRE(poppedGroupIds == std::set<std::string>{"group-a", "group-b"});
	REQUIRE(reader.indexerCommandCount() == 0);
}
}

TEST_CASE("ipc integration: full indexer workflow")
{
	using enum IndexerCommandType;
	const std::string uuid = "ipc_integ_test";
	const ProcessId mainPid = ProcessId::NONE;
	const ProcessId workerPid = static_cast<ProcessId>(1);

	SECTION("command push-pop round-trip")
	{
		if constexpr (language_packages::buildCxxLanguagePackage)
		{
			IpcInterprocessIndexerCommandManager ownerMgr(uuid, mainPid, true);
			ownerMgr.clearIndexerCommands();

			std::vector<std::shared_ptr<IndexerCommand>> cmds;
			cmds.push_back(makeOptionalCxxCommand(
				"/src/a.cpp",
				std::set<FilePath>{FilePath("/usr/include")},
				"/build",
				std::vector<std::string>{"-std=c++20", "-Wall"}));
			cmds.push_back(makeOptionalCxxCommand(
				"/src/b.cpp",
				std::set<FilePath>{FilePath("/usr/include")},
				"/build",
				std::vector<std::string>{"-std=c++20"}));

			ownerMgr.pushIndexerCommands(cmds);
			REQUIRE(ownerMgr.indexerCommandCount() == 2);

			// Simulate worker popping
			IpcInterprocessIndexerCommandManager workerMgr(uuid, workerPid, false);
			const auto cmd1 = workerMgr.popIndexerCommand();
			REQUIRE(cmd1 != nullptr);
			REQUIRE(cmd1->getSourceFilePath().str() == "/src/a.cpp");

			const auto cmd2 = workerMgr.popIndexerCommand();
			REQUIRE(cmd2 != nullptr);
			REQUIRE(cmd2->getSourceFilePath().str() == "/src/b.cpp");

			const auto cmd3 = workerMgr.popIndexerCommand();
			REQUIRE(cmd3 == nullptr);
			REQUIRE(ownerMgr.indexerCommandCount() == 0);
		}
		else
			SUCCEED("CXX language package disabled.");
	}

	SECTION("intermediate storage push-pop round-trip")
	{
		IpcInterprocessIntermediateStorageManager ownerMgr(uuid, workerPid, true);

		auto storage1 = std::make_shared<IntermediateStorage>();
		storage1->addNode(StorageNodeData(NODE_CLASS, "ClassA"));
		storage1->addFile(StorageFile(0, "/src/a.cpp", "cpp", "2025-01-01", true, true));

		auto storage2 = std::make_shared<IntermediateStorage>();
		storage2->addNode(StorageNodeData(NODE_FUNCTION, "funcB"));
		storage2->addError(StorageErrorData("error msg", "b.cpp", false, true));

		ownerMgr.pushIntermediateStorage(storage1);
		REQUIRE(ownerMgr.getIntermediateStorageCount() == 1);

		ownerMgr.pushIntermediateStorage(storage2);
		REQUIRE(ownerMgr.getIntermediateStorageCount() == 2);

		// Pop first
		auto popped1 = ownerMgr.popIntermediateStorage();
		REQUIRE(popped1 != nullptr);
		REQUIRE(popped1->getStorageNodes().size() == 1);
		REQUIRE(popped1->getStorageNodes()[0].serializedName == "ClassA");
		REQUIRE(popped1->getStorageFiles().size() == 1);
		REQUIRE(ownerMgr.getIntermediateStorageCount() == 1);

		// Pop second
		auto popped2 = ownerMgr.popIntermediateStorage();
		REQUIRE(popped2 != nullptr);
		REQUIRE(popped2->getStorageNodes().size() == 1);
		REQUIRE(popped2->getStorageNodes()[0].serializedName == "funcB");
		REQUIRE(popped2->getErrors().size() == 1);
		REQUIRE(ownerMgr.getIntermediateStorageCount() == 0);

		// Pop empty
		REQUIRE(ownerMgr.popIntermediateStorage() == nullptr);
	}

	SECTION("indexing status workflow")
	{
		IpcInterprocessIndexingStatusManager ownerMgr(uuid, mainPid, true);
		IpcInterprocessIndexingStatusManager workerMgr(uuid, workerPid, false);

		// Worker starts indexing a file
		workerMgr.startIndexingSourceFile(FilePath("/src/a.cpp"));

		// Main process reads status
		auto files = ownerMgr.getCurrentlyIndexedSourceFilePaths();
		REQUIRE(files.size() == 1);
		REQUIRE(files[0].str() == "/src/a.cpp");

		// Worker finishes
		workerMgr.finishIndexingSourceFile();

		// Main reads finished process
		ProcessId finished = ownerMgr.getNextFinishedProcessId();
		REQUIRE(finished == workerPid);
		REQUIRE(ownerMgr.getNextFinishedProcessId() == ProcessId::NONE);

		// Interrupt
		ownerMgr.setIndexingInterrupted(true);
		REQUIRE(workerMgr.getIndexingInterrupted() == true);
	}

	SECTION("simulated multi-threaded indexing workflow")
	{
		if constexpr (language_packages::buildCxxLanguagePackage)
		{
			IpcInterprocessIndexerCommandManager cmdMgr(uuid, mainPid, true);
			IpcInterprocessIndexingStatusManager statusMgr(uuid, mainPid, true);
			cmdMgr.clearIndexerCommands();

			// Main pushes 4 commands
			std::vector<std::shared_ptr<IndexerCommand>> cmds;
			for (int i = 0; i < 4; i++)
				cmds.push_back(makeOptionalCxxCommand(
					"/src/file" + std::to_string(i) + ".cpp",
					std::set<FilePath>{},
					"/build",
					std::vector<std::string>{"-std=c++20"}));
			cmdMgr.pushIndexerCommands(cmds);

			// Two worker threads
			std::atomic<int> processed{0};
			auto workerFn = [&](ProcessId pid) {
				IpcInterprocessIndexerCommandManager workerCmd(uuid, pid, false);
				IpcInterprocessIntermediateStorageManager workerStorage(uuid, pid, true);
				IpcInterprocessIndexingStatusManager workerStatus(uuid, pid, false);

				while (auto cmd = workerCmd.popIndexerCommand())
				{
					workerStatus.startIndexingSourceFile(cmd->getSourceFilePath());

					// Simulate indexing: create a small storage
					auto storage = std::make_shared<IntermediateStorage>();
					storage->addNode(StorageNodeData(NODE_FILE, cmd->getSourceFilePath().str()));
					workerStorage.pushIntermediateStorage(storage);

					workerStatus.finishIndexingSourceFile();
					processed++;
				}
			};

			std::thread t1(workerFn, static_cast<ProcessId>(1));
			std::thread t2(workerFn, static_cast<ProcessId>(2));
			t1.join();
			t2.join();

			REQUIRE(processed.load() == 4);
			REQUIRE(cmdMgr.indexerCommandCount() == 0);
		}
		else
			SUCCEED("CXX language package disabled.");
	}

	SECTION("pop with skip types leaves rust/swift commands in queue")
	{
		if constexpr (
			language_packages::buildCxxLanguagePackage &&
			(language_packages::buildRustLanguagePackage || language_packages::buildSwiftLanguagePackage))
		{
			const std::string skipUuid = "ipc_integ_skip_test";
			IpcInterprocessIndexerCommandManager ownerMgr(skipUuid, mainPid, true);
			ownerMgr.clearIndexerCommands();

			std::vector<std::shared_ptr<IndexerCommand>> commands;
			if constexpr (language_packages::buildRustLanguagePackage)
				commands.push_back(makeOptionalRustCommand("/rust/pkg"));
			if constexpr (language_packages::buildSwiftLanguagePackage)
				commands.push_back(makeOptionalSwiftCommand("/swift/pkg"));
			commands.push_back(makeOptionalCxxCommand(
				"/src/main.cpp",
				std::set<FilePath>{},
				"/build",
				std::vector<std::string>{"-std=c++20"}));

			ownerMgr.pushIndexerCommands(commands);

			IpcInterprocessIndexerCommandManager workerMgr(skipUuid, workerPid, false);
			std::set<IndexerCommandType> skipTypes;
			if constexpr (language_packages::buildRustLanguagePackage)
				skipTypes.insert(INDEXER_COMMAND_RUST);
			if constexpr (language_packages::buildSwiftLanguagePackage)
				skipTypes.insert(INDEXER_COMMAND_SWIFT);

			const std::shared_ptr<IndexerCommand> popped = workerMgr.popIndexerCommand(skipTypes);
			REQUIRE(popped != nullptr);
			REQUIRE(popped->getIndexerCommandType() == INDEXER_COMMAND_CXX);
			REQUIRE(workerMgr.indexerCommandCount() == commands.size() - 1);
			if constexpr (language_packages::buildRustLanguagePackage)
				REQUIRE(workerMgr.hasIndexerCommandType(INDEXER_COMMAND_RUST));
			if constexpr (language_packages::buildSwiftLanguagePackage)
				REQUIRE(workerMgr.hasIndexerCommandType(INDEXER_COMMAND_SWIFT));
		}
		else
			SUCCEED("Requires CXX plus Rust or Swift language package.");
	}

	SECTION("rust indexer subprocess processes rust command")
	{
		if constexpr (language_packages::buildRustLanguagePackage)
		{
			const std::string subprocessUuid = "ipc_rust_subprocess_test";
			const ProcessId rustProcessId = static_cast<ProcessId>(16);
			const std::string rustWorkingDirectory =
				"/__sourcetrail_nonexistent_rust_crate__";

			IpcInterprocessIndexerCommandManager commandOwner(subprocessUuid, mainPid, true);
			commandOwner.clearIndexerCommands();

			IpcInterprocessIndexingStatusManager statusOwner(subprocessUuid, mainPid, true);
			statusOwner.setIndexingInterrupted(false);
			statusOwner.setQueueStopped(true);

			IpcInterprocessIntermediateStorageManager storageOwner(subprocessUuid, rustProcessId, true);

			commandOwner.pushIndexerCommands({makeOptionalRustCommand(rustWorkingDirectory)});
			REQUIRE(commandOwner.indexerCommandCount() == 1);

			const std::string rustIndexerName =
				"sourcetrail_rust_indexer" + FilePath::getExecutableExtension();

			std::vector<FilePath> rustIndexerCandidates;
			rustIndexerCandidates.push_back(AppPath::getRustIndexerFilePath());
			rustIndexerCandidates.push_back(
				FilePath("../../app").getAbsolute().getConcatenated(rustIndexerName));
			rustIndexerCandidates.push_back(
				FilePath("../app").getAbsolute().getConcatenated(rustIndexerName));
			rustIndexerCandidates.push_back(
				FilePath("app").getAbsolute().getConcatenated(rustIndexerName));

			FilePath rustIndexerPath;
			for (const FilePath& candidate: rustIndexerCandidates)
			{
				if (!candidate.exists())
					continue;

				rustIndexerPath = candidate;
				break;
			}

			INFO("Rust indexer candidate paths:");
			for (const FilePath& candidate: rustIndexerCandidates)
				INFO("  - " + candidate.str());

			REQUIRE(!rustIndexerPath.empty());

			const utility::ProcessOutput processOutput = utility::executeProcess(
				rustIndexerPath.str(),
				{
					std::to_string(static_cast<std::size_t>(rustProcessId)),
					subprocessUuid,
					std::string{},
					std::string{},
					std::string{}
				},
				FilePath(),
				false,
				std::chrono::seconds(15));

			INFO("Rust indexer stdout:\n" + processOutput.output);
			INFO("Rust indexer stderr:\n" + processOutput.error);
			INFO("Rust indexer process info:\n" + processOutput.processInfo);
			REQUIRE(processOutput.exitCode == 0);

			REQUIRE(commandOwner.indexerCommandCount() == 0);

			const std::vector<FilePath> indexedFiles = statusOwner.getCurrentlyIndexedSourceFilePaths();
			REQUIRE(indexedFiles.size() == 1);
			REQUIRE(indexedFiles.front().str() == rustWorkingDirectory);

			REQUIRE(statusOwner.getNextFinishedProcessId() == rustProcessId);
			REQUIRE(statusOwner.getCurrentSourceFilePathForProcess(rustProcessId).has_value() == false);

			REQUIRE(storageOwner.getIntermediateStorageCount() == 1);
			const std::shared_ptr<IntermediateStorage> storage = storageOwner.popIntermediateStorage();
			REQUIRE(storage != nullptr);
			REQUIRE(storage->getErrors().empty() == false);
			REQUIRE(storageOwner.getIntermediateStorageCount() == 0);
		}
		else
			SUCCEED("Rust language package disabled.");
	}

	SECTION("task build index runs rust subprocess and drains storage")
	{
		if constexpr (language_packages::buildRustLanguagePackage)
		{
			using enum Task::TaskState;
			const std::string taskUuid = "ipc_task_build_rust_subprocess_test";
			const std::string rustWorkingDirectory =
				"/__sourcetrail_nonexistent_rust_task_crate__";

			IpcInterprocessIndexerCommandManager commandOwner(taskUuid, mainPid, true);
			commandOwner.clearIndexerCommands();
			commandOwner.pushIndexerCommands({makeOptionalRustCommand(rustWorkingDirectory)});
			REQUIRE(commandOwner.indexerCommandCount() == 1);

			auto storageProvider = std::make_shared<StorageProvider>();
			auto dialogView = std::make_shared<DialogView>(DialogView::UseCase::INDEXING, nullptr);
			TaskBuildIndex task(0, storageProvider, dialogView, taskUuid);

			auto blackboard = std::make_shared<Blackboard>();
			blackboard->set<int>("source_file_count", 1);
			blackboard->set<int>("indexed_source_file_count", 0);
			blackboard->set<bool>("indexer_command_queue_stopped", true);

			Task::TaskState taskState = STATE_RUNNING;
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
			while (
				taskState == STATE_RUNNING &&
				std::chrono::steady_clock::now() < deadline)
				taskState = task.update(blackboard);

			REQUIRE(taskState == STATE_SUCCESS);
			REQUIRE(commandOwner.indexerCommandCount() == 0);

			bool threadsStopped = false;
			REQUIRE(blackboard->get<bool>("indexer_threads_stopped", threadsStopped));
			REQUIRE(threadsStopped == true);

			int indexedSourceFileCount = 0;
			REQUIRE(blackboard->get<int>("indexed_source_file_count", indexedSourceFileCount));
			REQUIRE(indexedSourceFileCount == 1);

			REQUIRE(storageProvider->getStorageCount() == 1);
			const std::shared_ptr<IntermediateStorage> storage = storageProvider->consumeLargestStorage();
			REQUIRE(storage != nullptr);
			REQUIRE(storage->getErrors().empty() == false);
			REQUIRE(storageProvider->getStorageCount() == 0);
		}
		else
			SUCCEED("Rust language package disabled.");
	}

	SECTION("swift indexer subprocess processes swift command")
	{
		if constexpr (language_packages::buildSwiftLanguagePackage)
		{
			const std::string subprocessUuid = "ipc_swift_subprocess_test";
			const ProcessId swiftProcessId = static_cast<ProcessId>(17);

			IpcInterprocessIndexerCommandManager commandOwner(subprocessUuid, mainPid, true);
			commandOwner.clearIndexerCommands();

			IpcInterprocessIndexingStatusManager statusOwner(subprocessUuid, mainPid, true);
			statusOwner.setIndexingInterrupted(false);
			statusOwner.setQueueStopped(true);

			IpcInterprocessIntermediateStorageManager storageOwner(subprocessUuid, swiftProcessId, true);

			commandOwner.pushIndexerCommands({
				std::make_shared<IndexerCommandSwift>(
					FilePath("/swift/pkg/main.swift"),
					std::set<FilePath>{FilePath("/swift/pkg")},
					FilePath("/swift/pkg"))
			});
			REQUIRE(commandOwner.indexerCommandCount() == 1);

			const std::string swiftIndexerName =
				"sourcetrail_swift_indexer" + FilePath::getExecutableExtension();

			std::vector<FilePath> swiftIndexerCandidates;
			swiftIndexerCandidates.push_back(AppPath::getSwiftIndexerFilePath());
			swiftIndexerCandidates.push_back(
				FilePath("../../app").getAbsolute().getConcatenated(swiftIndexerName));
			swiftIndexerCandidates.push_back(
				FilePath("../app").getAbsolute().getConcatenated(swiftIndexerName));
			swiftIndexerCandidates.push_back(
				FilePath("app").getAbsolute().getConcatenated(swiftIndexerName));

			FilePath swiftIndexerPath;
			for (const FilePath& candidate: swiftIndexerCandidates)
			{
				if (!candidate.exists())
					continue;

				swiftIndexerPath = candidate;
				break;
			}

			INFO("Swift indexer candidate paths:");
			for (const FilePath& candidate: swiftIndexerCandidates)
				INFO("  - " + candidate.str());

			REQUIRE(!swiftIndexerPath.empty());

			const utility::ProcessOutput processOutput = utility::executeProcess(
				swiftIndexerPath.str(),
				{
					std::to_string(static_cast<std::size_t>(swiftProcessId)),
					subprocessUuid,
					std::string{},
					std::string{},
					std::string{}
				},
				FilePath(),
				false,
				std::chrono::seconds(15));

			INFO("Swift indexer stdout:\n" + processOutput.output);
			INFO("Swift indexer stderr:\n" + processOutput.error);
			INFO("Swift indexer process info:\n" + processOutput.processInfo);
			REQUIRE(processOutput.exitCode == 0);

			REQUIRE(commandOwner.indexerCommandCount() == 0);

			const std::vector<FilePath> indexedFiles = statusOwner.getCurrentlyIndexedSourceFilePaths();
			REQUIRE(indexedFiles.size() == 1);
			REQUIRE(indexedFiles.front().str() == "/swift/pkg/main.swift");

			REQUIRE(statusOwner.getNextFinishedProcessId() == swiftProcessId);
			REQUIRE(statusOwner.getCurrentSourceFilePathForProcess(swiftProcessId).has_value() == false);

			REQUIRE(storageOwner.getIntermediateStorageCount() == 1);
			const std::shared_ptr<IntermediateStorage> storage = storageOwner.popIntermediateStorage();
			REQUIRE(storage != nullptr);
			REQUIRE(storage->getNextId() == 1);
			REQUIRE(storage->getStorageNodes().empty());
			REQUIRE(storage->getStorageFiles().empty());
			REQUIRE(storage->getStorageEdges().empty());
			REQUIRE(storage->getStorageSymbols().empty());
			REQUIRE(storage->getStorageLocalSymbols().empty());
			REQUIRE(storage->getStorageSourceLocations().empty());
			REQUIRE(storage->getStorageOccurrences().empty());
			REQUIRE(storage->getComponentAccesses().empty());
			REQUIRE(storage->getErrors().empty());
			REQUIRE(storageOwner.getIntermediateStorageCount() == 0);
		}
		else
			SUCCEED("Swift language package disabled.");
	}

	SECTION("task build index runs swift subprocess and drains storage")
	{
		if constexpr (language_packages::buildSwiftLanguagePackage)
		{
			using enum Task::TaskState;
			const std::string taskUuid = "ipc_task_build_swift_subprocess_test";

			IpcInterprocessIndexerCommandManager commandOwner(taskUuid, mainPid, true);
			commandOwner.clearIndexerCommands();
			commandOwner.pushIndexerCommands({
				std::make_shared<IndexerCommandSwift>(
					FilePath("/swift/pkg/task_main.swift"),
					std::set<FilePath>{FilePath("/swift/pkg")},
					FilePath("/swift/pkg"))
			});
			REQUIRE(commandOwner.indexerCommandCount() == 1);

			auto storageProvider = std::make_shared<StorageProvider>();
			auto dialogView = std::make_shared<DialogView>(DialogView::UseCase::INDEXING, nullptr);
			TaskBuildIndex task(0, storageProvider, dialogView, taskUuid);

			auto blackboard = std::make_shared<Blackboard>();
			blackboard->set<int>("source_file_count", 1);
			blackboard->set<int>("indexed_source_file_count", 0);
			blackboard->set<bool>("indexer_command_queue_stopped", true);

			Task::TaskState taskState = STATE_RUNNING;
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
			while (
				taskState == STATE_RUNNING &&
				std::chrono::steady_clock::now() < deadline)
				taskState = task.update(blackboard);

			REQUIRE(taskState == STATE_SUCCESS);
			REQUIRE(commandOwner.indexerCommandCount() == 0);

			bool threadsStopped = false;
			REQUIRE(blackboard->get<bool>("indexer_threads_stopped", threadsStopped));
			REQUIRE(threadsStopped == true);

			int indexedSourceFileCount = 0;
			REQUIRE(blackboard->get<int>("indexed_source_file_count", indexedSourceFileCount));
			REQUIRE(indexedSourceFileCount == 1);

			REQUIRE(storageProvider->getStorageCount() == 1);
			const std::shared_ptr<IntermediateStorage> storage = storageProvider->consumeLargestStorage();
			REQUIRE(storage != nullptr);
			REQUIRE(storage->getNextId() == 1);
			REQUIRE(storage->getStorageNodes().empty());
			REQUIRE(storage->getStorageFiles().empty());
			REQUIRE(storage->getStorageEdges().empty());
			REQUIRE(storage->getStorageSymbols().empty());
			REQUIRE(storage->getStorageLocalSymbols().empty());
			REQUIRE(storage->getStorageSourceLocations().empty());
			REQUIRE(storage->getStorageOccurrences().empty());
			REQUIRE(storage->getComponentAccesses().empty());
			REQUIRE(storage->getErrors().empty());
			REQUIRE(storageProvider->getStorageCount() == 0);
		}
		else
			SUCCEED("Swift language package disabled.");
	}

	SECTION("task fill queue deduplicates rust commands by working directory")
	{
		if constexpr (!language_packages::buildRustLanguagePackage)
		{
			SUCCEED("Rust language package disabled.");
			return;
		}

		assertTaskFillQueueDeduplicatesLanguageCommandsByWorkingDirectory(
			"ipc_fill_rust_dedup_test",
			mainPid,
			workerPid,
			makeOptionalRustCommand,
			isOptionalRustCommand,
			getOptionalRustWorkingDirectory,
			"/crate/a",
			"/crate/b",
			"/src/a.cpp");
	}

	SECTION("task fill queue deduplicates swift commands by working directory")
	{
		if constexpr (!language_packages::buildSwiftLanguagePackage)
		{
			SUCCEED("Swift language package disabled.");
			return;
		}

		assertTaskFillQueueDeduplicatesLanguageCommandsByWorkingDirectory(
			"ipc_fill_swift_dedup_test",
			mainPid,
			workerPid,
			makeOptionalSwiftCommand,
			isOptionalSwiftCommand,
			getOptionalSwiftWorkingDirectory,
			"/swift/pkg/a",
			"/swift/pkg/b",
			"/src/swift_bridge.cpp");
	}
}

// Fan-out S3 gate: with >= 2 source groups the fill task tops up the SHM queue
// per group, so a pinned consumer's group is refilled even while another
// group's commands sit in the queue (the legacy total-size fill would starve it).
TEST_CASE("ipc integration: group-aware queue fill keeps every group available")
{
	// The task's own command manager is the sole segment OWNER here: a second
	// CREATE_AND_DELETE manager in the same process would re-create the segment
	// underneath it and dangle its mutex view (pre-existing IpcSharedMemory
	// limitation).
	const std::string uuid = "gfill_s3";

	auto makeCommands = [](const std::string& prefix, int commandCount) {
		std::vector<std::shared_ptr<IndexerCommand>> commands;
		for (int i = 0; i < commandCount; i++)
		{
			// Distinct working directories: Swift commands deduplicate by
			// working directory when the Swift package is compiled in.
			commands.push_back(std::make_shared<IndexerCommandSwift>(
				FilePath("/" + prefix + std::to_string(i) + ".swift"),
				std::set<FilePath>{},
				FilePath("/wd/" + prefix + std::to_string(i))));
		}
		return commands;
	};

	auto provider = std::make_unique<CombinedIndexerCommandProvider>();
	provider->addProvider(
		std::make_shared<MemoryIndexerCommandProvider>(makeCommands("a", 5)), "group-a");
	provider->addProvider(
		std::make_shared<MemoryIndexerCommandProvider>(makeCommands("b", 5)), "group-b");

	auto blackboard = std::make_shared<Blackboard>();
	blackboard->set<int>("source_file_count", 10);
	TaskFillIndexerCommandsQueue task(uuid, std::move(provider), 2);
	IpcInterprocessIndexerCommandManager reader(uuid, static_cast<ProcessId>(1), false);

	// First fill: 2 commands per group, not 2 in total.
	REQUIRE(task.update(blackboard) == Task::TaskState::STATE_RUNNING);
	{
		auto counts = reader.indexerCommandCountsBySourceGroup();
		REQUIRE(counts["group-a"] == 2);
		REQUIRE(counts["group-b"] == 2);
	}

	// A pinned consumer drains its group; the next fill restocks that group
	// even though the other group's commands still occupy the queue.
	const std::set<IndexerCommandType> noSkips;
	REQUIRE(reader.popIndexerCommandBlocking(noSkips, 10, "group-b"));
	REQUIRE(reader.popIndexerCommandBlocking(noSkips, 10, "group-b"));
	REQUIRE(reader.popIndexerCommandBlocking(noSkips, 10, "group-b") == nullptr);

	REQUIRE(task.update(blackboard) == Task::TaskState::STATE_RUNNING);
	{
		auto counts = reader.indexerCommandCountsBySourceGroup();
		REQUIRE(counts["group-a"] == 2);
		REQUIRE(counts["group-b"] == 2);
	}

	// Drain everything: both groups deliver all their commands exactly once.
	std::map<std::string, int> popped;
	popped["group-b"] = 2;	// already popped above
	for (;;)
	{
		while (const std::shared_ptr<IndexerCommand> command =
				   reader.popIndexerCommandBlocking(noSkips, 10))
		{
			popped[command->getSourceGroupId()]++;
		}
		if (task.update(blackboard) == Task::TaskState::STATE_SUCCESS)
			break;
	}
	while (const std::shared_ptr<IndexerCommand> command =
			   reader.popIndexerCommandBlocking(noSkips, 10))
	{
		popped[command->getSourceGroupId()]++;
	}
	REQUIRE(popped["group-a"] == 5);
	REQUIRE(popped["group-b"] == 5);
	REQUIRE(reader.indexerCommandCount() == 0);
}

// Fan-out S2 gate: a consumer pinned to a source group only pops that group's
// commands; an unpinned consumer accepts any group (legacy behavior).
TEST_CASE("ipc integration: per-group command pop filter")
{
	const std::string uuid = "gpin_s2";
	const ProcessId mainPid = ProcessId::NONE;
	const ProcessId workerPid = static_cast<ProcessId>(1);

	IpcInterprocessIndexerCommandManager ownerMgr(uuid, mainPid, true);
	ownerMgr.clearIndexerCommands();

	auto makeCommand = [](const std::string& path, const std::string& groupId) {
		auto command = std::make_shared<IndexerCommandSwift>(
			FilePath(path), std::set<FilePath>{}, FilePath("/wd"));
		command->setSourceGroupId(groupId);
		return std::static_pointer_cast<IndexerCommand>(command);
	};

	ownerMgr.pushIndexerCommands({
		makeCommand("/a1.swift", "group-a"),
		makeCommand("/b1.swift", "group-b"),
		makeCommand("/a2.swift", "group-a"),
	});

	IpcInterprocessIndexerCommandManager reader(uuid, workerPid, false);
	const std::set<IndexerCommandType> noSkips;

	// The pinned consumer receives its group's command even though another
	// group's command is ahead of it in the queue.
	const std::shared_ptr<IndexerCommand> pinned =
		reader.popIndexerCommandBlocking(noSkips, 10, "group-b");
	REQUIRE(pinned);
	REQUIRE(pinned->getSourceGroupId() == "group-b");
	REQUIRE(pinned->getSourceFilePath().str() == "/b1.swift");

	// Its group drained, the pinned consumer gets nothing (after the liveness
	// timeout) and the other group's commands stay untouched.
	REQUIRE(reader.popIndexerCommandBlocking(noSkips, 10, "group-b") == nullptr);
	REQUIRE(reader.indexerCommandCount() == 2);

	// An unpinned consumer ("" filter) drains the remaining commands.
	const std::shared_ptr<IndexerCommand> first = reader.popIndexerCommandBlocking(noSkips, 10);
	const std::shared_ptr<IndexerCommand> second =
		reader.popIndexerCommandBlocking(noSkips, 10, "");
	REQUIRE(first);
	REQUIRE(second);
	REQUIRE(first->getSourceGroupId() == "group-a");
	REQUIRE(second->getSourceGroupId() == "group-a");
	REQUIRE(reader.indexerCommandCount() == 0);
	ownerMgr.clearIndexerCommands();
}
