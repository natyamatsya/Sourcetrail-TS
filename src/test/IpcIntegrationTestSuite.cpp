#include "Catch2.hpp"

#include "language_packages.h"

#include <thread>

#include "IpcInterprocessIndexerCommandManager.h"
#include "IpcInterprocessIntermediateStorageManager.h"
#include "IpcInterprocessIndexingStatusManager.h"
#include "IpcSharedMemoryGarbageCollector.h"

#include "IntermediateStorage.h"
#if BUILD_CXX_LANGUAGE_PACKAGE
#include "IndexerCommandCxx.h"
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
}
