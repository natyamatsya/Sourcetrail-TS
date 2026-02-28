#include "Catch2.hpp"

#include "language_packages.h"

#include <thread>

#include "Blackboard.h"
#include "CombinedIndexerCommandProvider.h"
#include "IpcInterprocessIndexerCommandManager.h"
#include "IpcInterprocessIntermediateStorageManager.h"
#include "IpcInterprocessIndexingStatusManager.h"
#include "IpcSharedMemoryGarbageCollector.h"
#include "MemoryIndexerCommandProvider.h"
#include "TaskFillIndexerCommandQueue.h"

#include "IntermediateStorage.h"
#if BUILD_CXX_LANGUAGE_PACKAGE
#include "IndexerCommandCxx.h"
#endif
#if BUILD_RUST_LANGUAGE_PACKAGE
#include "IndexerCommandRust.h"
#endif

TEST_CASE("ipc integration: full indexer workflow")
{
	const std::string uuid = "ipc_integ_test";
	const ProcessId mainPid = ProcessId::NONE;
	const ProcessId workerPid = static_cast<ProcessId>(1);

	SECTION("command push-pop round-trip")
	{
		IpcInterprocessIndexerCommandManager ownerMgr(uuid, mainPid, true);
		ownerMgr.clearIndexerCommands();

#if BUILD_CXX_LANGUAGE_PACKAGE
		std::vector<std::shared_ptr<IndexerCommand>> cmds;
		cmds.push_back(std::make_shared<IndexerCommandCxx>(
			FilePath("/src/a.cpp"),
			std::set<FilePath>{FilePath("/usr/include")},
			std::set<FilePathFilter>{},
			std::set<FilePathFilter>{},
			FilePath("/build"),
			std::vector<std::string>{"-std=c++20", "-Wall"},
			std::string{}));
		cmds.push_back(std::make_shared<IndexerCommandCxx>(
			FilePath("/src/b.cpp"),
			std::set<FilePath>{FilePath("/usr/include")},
			std::set<FilePathFilter>{},
			std::set<FilePathFilter>{},
			FilePath("/build"),
			std::vector<std::string>{"-std=c++20"},
			std::string{}));

		ownerMgr.pushIndexerCommands(cmds);
		REQUIRE(ownerMgr.indexerCommandCount() == 2);

		// Simulate worker popping
		IpcInterprocessIndexerCommandManager workerMgr(uuid, workerPid, false);
		auto cmd1 = workerMgr.popIndexerCommand();
		REQUIRE(cmd1 != nullptr);
		REQUIRE(cmd1->getSourceFilePath().str() == "/src/a.cpp");

		auto cmd2 = workerMgr.popIndexerCommand();
		REQUIRE(cmd2 != nullptr);
		REQUIRE(cmd2->getSourceFilePath().str() == "/src/b.cpp");

		auto cmd3 = workerMgr.popIndexerCommand();
		REQUIRE(cmd3 == nullptr);
		REQUIRE(ownerMgr.indexerCommandCount() == 0);
#endif
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
		IpcInterprocessIndexerCommandManager cmdMgr(uuid, mainPid, true);
		IpcInterprocessIndexingStatusManager statusMgr(uuid, mainPid, true);
		cmdMgr.clearIndexerCommands();

#if BUILD_CXX_LANGUAGE_PACKAGE
		// Main pushes 4 commands
		std::vector<std::shared_ptr<IndexerCommand>> cmds;
		for (int i = 0; i < 4; i++)
			cmds.push_back(std::make_shared<IndexerCommandCxx>(
				FilePath("/src/file" + std::to_string(i) + ".cpp"),
				std::set<FilePath>{}, std::set<FilePathFilter>{}, std::set<FilePathFilter>{},
				FilePath("/build"), std::vector<std::string>{"-std=c++20"},
				std::string{}));
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
#endif
	}

#if BUILD_RUST_LANGUAGE_PACKAGE
	SECTION("task fill queue deduplicates rust commands by working directory")
	{
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
#if BUILD_CXX_LANGUAGE_PACKAGE
		providerACommands.push_back(std::make_shared<IndexerCommandCxx>(
			FilePath("/src/a.cpp"),
			std::set<FilePath>{},
			std::set<FilePathFilter>{},
			std::set<FilePathFilter>{},
			FilePath("/build"),
			std::vector<std::string>{"-std=c++20"},
			std::string{}));
#endif
		provider->addProvider(
			std::make_shared<MemoryIndexerCommandProvider>(providerACommands));

		provider->addProvider(std::make_shared<MemoryIndexerCommandProvider>(
			std::vector<std::shared_ptr<IndexerCommand>>{
				makeRustCommand("/crate/a"),
				makeRustCommand("/crate/b")}));

		auto blackboard = std::make_shared<Blackboard>();
		blackboard->set<int>("source_file_count", static_cast<int>(provider->size()));

		TaskFillIndexerCommandsQueue task(dedupUuid, std::move(provider), 16);
		REQUIRE(task.update(blackboard) == Task::STATE_SUCCESS);

		int sourceFileCount = 0;
		REQUIRE(blackboard->get<int>("source_file_count", sourceFileCount));
#if BUILD_CXX_LANGUAGE_PACKAGE
		REQUIRE(sourceFileCount == 3);
#else
		REQUIRE(sourceFileCount == 2);
#endif

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

#if BUILD_CXX_LANGUAGE_PACKAGE
			if (command->getIndexerCommandType() == INDEXER_COMMAND_CXX)
				cxxCount++;
#endif
		}

		REQUIRE(rustCount == 2);
		REQUIRE(rustWorkingDirectories.size() == 2);
#if BUILD_CXX_LANGUAGE_PACKAGE
		REQUIRE(cxxCount == 1);
#endif
		REQUIRE(reader.indexerCommandCount() == 0);
	}
#endif
}
