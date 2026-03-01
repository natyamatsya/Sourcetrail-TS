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
#include "StorageProvider.h"
#include "TaskBuildIndex.h"
#include "TaskFillIndexerCommandQueue.h"
#include "utilityApp.h"

#include "IntermediateStorage.h"

#include "IndexerCommandRust.h"
#include "IndexerCommandSwift.h"

namespace
{
int withOptionalCxxCount(const int& count)
{
	if constexpr (language_packages::buildCxxLanguagePackage)
		return count + 1;
	return count;
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
				commands.push_back(std::make_shared<IndexerCommandRust>(
					FilePath("/rust/pkg"),
					std::set<FilePath>{FilePath("/rust/pkg")},
					FilePath("/rust/pkg")));
			if constexpr (language_packages::buildSwiftLanguagePackage)
				commands.push_back(std::make_shared<IndexerCommandSwift>(
					FilePath("/swift/pkg"),
					std::set<FilePath>{FilePath("/swift/pkg")},
					FilePath("/swift/pkg")));
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
		if constexpr (language_packages::buildRustLanguagePackage)
		{
			using enum Task::TaskState;
			const std::string dedupUuid = "ipc_fill_rust_dedup_test";
			IpcInterprocessIndexerCommandManager ownerMgr(dedupUuid, mainPid, true);
			ownerMgr.clearIndexerCommands();

			auto makeRustCommand = [](const std::string& workingDirectory) {
				const FilePath workingDirPath(workingDirectory);
				return std::make_shared<IndexerCommandRust>(
					workingDirPath,
					std::set<FilePath>{workingDirPath},
					workingDirPath);
			};

			auto provider = std::make_unique<CombinedIndexerCommandProvider>();

			std::vector<std::shared_ptr<IndexerCommand>> providerACommands;
			providerACommands.push_back(makeRustCommand("/crate/a"));
			if (const std::shared_ptr<IndexerCommand> cxxCommand =
				makeOptionalCxxCommand(
					"/src/a.cpp",
					std::set<FilePath>{},
					"/build",
					std::vector<std::string>{"-std=c++20"});
				cxxCommand)
				providerACommands.push_back(cxxCommand);
			provider->addProvider(
				std::make_shared<MemoryIndexerCommandProvider>(providerACommands));

			provider->addProvider(std::make_shared<MemoryIndexerCommandProvider>(
				std::vector<std::shared_ptr<IndexerCommand>>{
					makeRustCommand("/crate/a"),
					makeRustCommand("/crate/b")}));

			auto blackboard = std::make_shared<Blackboard>();
			blackboard->set<int>("source_file_count", static_cast<int>(provider->size()));

			TaskFillIndexerCommandsQueue task(dedupUuid, std::move(provider), 16);
			REQUIRE(task.update(blackboard) == STATE_SUCCESS);

			int sourceFileCount = 0;
			REQUIRE(blackboard->get<int>("source_file_count", sourceFileCount));
			REQUIRE(
				sourceFileCount ==
				withOptionalCxxCount(2));

			IpcInterprocessIndexerCommandManager reader(dedupUuid, workerPid, false);

			size_t rustCount = 0;
			size_t cxxCount = 0;
			std::set<std::string> rustWorkingDirectories;
			while (std::shared_ptr<IndexerCommand> command = reader.popIndexerCommand())
			{
				if (command->getIndexerCommandType() == INDEXER_COMMAND_RUST)
				{
					rustCount++;
					const auto* rustCommand = dynamic_cast<const IndexerCommandRust*>(command.get());
					REQUIRE(rustCommand != nullptr);
					rustWorkingDirectories.insert(rustCommand->getWorkingDirectory().str());
					continue;
				}

				if (isOptionalCxxCommand(command))
					cxxCount++;
			}

			REQUIRE(rustCount == 2);
			REQUIRE(rustWorkingDirectories.size() == 2);
			REQUIRE(
				cxxCount ==
				static_cast<size_t>(withOptionalCxxCount(0)));
			REQUIRE(reader.indexerCommandCount() == 0);
		}
		else
			SUCCEED("Rust language package disabled.");
	}

	SECTION("task fill queue deduplicates swift commands by working directory")
	{
		if constexpr (language_packages::buildSwiftLanguagePackage)
		{
			using enum Task::TaskState;
			const std::string dedupUuid = "ipc_fill_swift_dedup_test";
			IpcInterprocessIndexerCommandManager ownerMgr(dedupUuid, mainPid, true);
			ownerMgr.clearIndexerCommands();

			auto makeSwiftCommand = [](const std::string& workingDirectory) {
				const FilePath workingDirPath(workingDirectory);
				return std::make_shared<IndexerCommandSwift>(
					workingDirPath,
					std::set<FilePath>{workingDirPath},
					workingDirPath);
			};

			auto provider = std::make_unique<CombinedIndexerCommandProvider>();

			std::vector<std::shared_ptr<IndexerCommand>> providerACommands;
			providerACommands.push_back(makeSwiftCommand("/swift/pkg/a"));
			if (const std::shared_ptr<IndexerCommand> cxxCommand =
				makeOptionalCxxCommand(
					"/src/swift_bridge.cpp",
					std::set<FilePath>{},
					"/build",
					std::vector<std::string>{"-std=c++20"});
				cxxCommand)
				providerACommands.push_back(cxxCommand);
			provider->addProvider(
				std::make_shared<MemoryIndexerCommandProvider>(providerACommands));

			provider->addProvider(std::make_shared<MemoryIndexerCommandProvider>(
				std::vector<std::shared_ptr<IndexerCommand>>{
					makeSwiftCommand("/swift/pkg/a"),
					makeSwiftCommand("/swift/pkg/b")}));

			auto blackboard = std::make_shared<Blackboard>();
			blackboard->set<int>("source_file_count", static_cast<int>(provider->size()));

			TaskFillIndexerCommandsQueue task(dedupUuid, std::move(provider), 16);
			REQUIRE(task.update(blackboard) == STATE_SUCCESS);

			int sourceFileCount = 0;
			REQUIRE(blackboard->get<int>("source_file_count", sourceFileCount));
			REQUIRE(
				sourceFileCount ==
				withOptionalCxxCount(2));

			IpcInterprocessIndexerCommandManager reader(dedupUuid, workerPid, false);

			size_t swiftCount = 0;
			size_t cxxCount = 0;
			std::set<std::string> swiftWorkingDirectories;
			while (std::shared_ptr<IndexerCommand> command = reader.popIndexerCommand())
			{
				if (command->getIndexerCommandType() == INDEXER_COMMAND_SWIFT)
				{
					swiftCount++;
					const auto* swiftCommand = dynamic_cast<const IndexerCommandSwift*>(command.get());
					REQUIRE(swiftCommand != nullptr);
					swiftWorkingDirectories.insert(swiftCommand->getWorkingDirectory().str());
					continue;
				}

				if (isOptionalCxxCommand(command))
					cxxCount++;
			}

			REQUIRE(swiftCount == 2);
			REQUIRE(swiftWorkingDirectories.size() == 2);
			REQUIRE(
				cxxCount ==
				static_cast<size_t>(withOptionalCxxCount(0)));
			REQUIRE(reader.indexerCommandCount() == 0);
		}
		else
			SUCCEED("Swift language package disabled.");
	}
}
