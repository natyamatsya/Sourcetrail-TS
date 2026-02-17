#include "Catch2.hpp"

#include "language_packages.h"

#include "IndexerCommandSerializer.h"
#include "IntermediateStorageSerializer.h"
#include "IndexingStatusSerializer.h"
#include "GarbageCollectorSerializer.h"

#include "IntermediateStorage.h"
#include "IndexerCommand.h"
#if BUILD_CXX_LANGUAGE_PACKAGE
#include "IndexerCommandCxx.h"
#endif

TEST_CASE("ipc serializer round-trips")
{
#if BUILD_CXX_LANGUAGE_PACKAGE
	SECTION("IndexerCommand CXX round-trip")
	{
		std::set<FilePath> indexedPaths = {FilePath("/usr/include"), FilePath("/opt/include")};
		std::set<FilePathFilter> excludeFilters = {FilePathFilter("build/*")};
		std::set<FilePathFilter> includeFilters = {FilePathFilter("src/*")};
		FilePath workingDir("/home/user/project");
		std::vector<std::string> flags = {"-std=c++17", "-Wall", "-DFOO=1"};

		auto cmd = std::make_shared<IndexerCommandCxx>(
			FilePath("/home/user/project/main.cpp"),
			indexedPaths, excludeFilters, includeFilters, workingDir, flags);

		std::vector<std::shared_ptr<IndexerCommand>> commands = {cmd};

		auto buf = IpcSerializer::serializeIndexerCommands(commands);
		auto result = IpcSerializer::deserializeIndexerCommands(buf.data(), buf.size());

		REQUIRE(result.size() == 1);
		auto* cxx = dynamic_cast<IndexerCommandCxx*>(result[0].get());
		REQUIRE(cxx != nullptr);
		REQUIRE(cxx->getSourceFilePath().str() == "/home/user/project/main.cpp");
		REQUIRE(cxx->getIndexedPaths() == indexedPaths);
		REQUIRE(cxx->getWorkingDirectory().str() == "/home/user/project");
		REQUIRE(cxx->getCompilerFlags() == flags);
		REQUIRE(cxx->getExcludeFilters().size() == 1);
		REQUIRE(cxx->getIncludeFilters().size() == 1);
	}
#endif

	SECTION("IntermediateStorage round-trip")
	{
		IntermediateStorage original;

		original.addNode(StorageNodeData(NODE_CLASS, "MyClass"));
		original.addNode(StorageNodeData(NODE_FUNCTION, "doSomething"));
		original.addFile(StorageFile(0, "/src/main.cpp", "cpp", "2025-01-01", true, true));
		original.addEdge(StorageEdgeData(Edge::EDGE_CALL, Id(1), Id(2)));
		original.addSymbol(StorageSymbol(Id(1), DefinitionKind::EXPLICIT));
		original.addSourceLocation(StorageSourceLocationData(Id(1), 10, 5, 10, 20, LocationType::TOKEN));
		original.addLocalSymbol(StorageLocalSymbolData("localVar"));
		original.addOccurrence(StorageOccurrence(Id(1), Id(1)));
		original.addComponentAccess(StorageComponentAccess(Id(1), AccessKind::PUBLIC));
		original.addError(StorageErrorData("undefined reference", "main.cpp", true, false));

		Id expectedNextId = original.getNextId();

		auto buf = IpcSerializer::serializeIntermediateStorage(original);
		auto result = IpcSerializer::deserializeIntermediateStorage(buf.data(), buf.size());

		REQUIRE(result != nullptr);
		REQUIRE(result->getNextId() == expectedNextId);
		REQUIRE(result->getStorageNodes().size() == 2);
		REQUIRE(result->getStorageFiles().size() == 1);
		REQUIRE(result->getStorageEdges().size() == 1);
		REQUIRE(result->getStorageSymbols().size() == 1);
		REQUIRE(result->getStorageSourceLocations().size() == 1);
		REQUIRE(result->getStorageLocalSymbols().size() == 1);
		REQUIRE(result->getStorageOccurrences().size() == 1);
		REQUIRE(result->getComponentAccesses().size() == 1);
		REQUIRE(result->getErrors().size() == 1);

		const auto& nodes = result->getStorageNodes();
		REQUIRE(nodes[0].type == NODE_CLASS);
		REQUIRE(nodes[0].serializedName == "MyClass");
		REQUIRE(nodes[1].type == NODE_FUNCTION);

		const auto& files = result->getStorageFiles();
		REQUIRE(files[0].filePath == "/src/main.cpp");
		REQUIRE(files[0].languageIdentifier == "cpp");
		REQUIRE(files[0].indexed == true);
		REQUIRE(files[0].complete == true);

		const auto& edges = result->getStorageEdges();
		REQUIRE(edges[0].type == Edge::EDGE_CALL);

		const auto& errors = result->getErrors();
		REQUIRE(errors[0].message == "undefined reference");
		REQUIRE(errors[0].translationUnit == "main.cpp");
		REQUIRE(errors[0].fatal == true);
		REQUIRE(errors[0].indexed == false);
	}

	SECTION("IndexingStatus round-trip")
	{
		IpcSerializer::IndexingStatusData original;
		original.indexingFilePaths = {"/src/a.cpp", "/src/b.cpp"};
		original.currentFiles = {{1, "/src/a.cpp"}, {2, "/src/b.cpp"}};
		original.crashedFilePaths = {"/src/crash.cpp"};
		original.finishedProcessIds = {3, 4, 5};
		original.indexingInterrupted = true;

		auto buf = IpcSerializer::serializeIndexingStatus(original);
		auto result = IpcSerializer::deserializeIndexingStatus(buf.data(), buf.size());

		REQUIRE(result.indexingFilePaths == original.indexingFilePaths);
		REQUIRE(result.currentFiles.size() == 2);
		REQUIRE(result.currentFiles[0].first == 1);
		REQUIRE(result.currentFiles[0].second == "/src/a.cpp");
		REQUIRE(result.currentFiles[1].first == 2);
		REQUIRE(result.crashedFilePaths == original.crashedFilePaths);
		REQUIRE(result.finishedProcessIds == original.finishedProcessIds);
		REQUIRE(result.indexingInterrupted == true);
	}

	SECTION("GarbageCollector round-trip")
	{
		IpcSerializer::GarbageCollectorData original;
		original.instances = {{"uuid-1", "2025-01-01 12:00:00"}, {"uuid-2", "2025-01-01 12:01:00"}};
		original.memoryTimestamps = {
			{"mem_indexer_1", "2025-01-01 12:00:05"},
			{"mem_storage_1", "2025-01-01 12:00:10"}};

		auto buf = IpcSerializer::serializeGarbageCollector(original);
		auto result = IpcSerializer::deserializeGarbageCollector(buf.data(), buf.size());

		REQUIRE(result.instances.size() == 2);
		REQUIRE(result.instances[0].first == "uuid-1");
		REQUIRE(result.instances[0].second == "2025-01-01 12:00:00");
		REQUIRE(result.instances[1].first == "uuid-2");

		REQUIRE(result.memoryTimestamps.size() == 2);
		REQUIRE(result.memoryTimestamps[0].first == "mem_indexer_1");
		REQUIRE(result.memoryTimestamps[1].second == "2025-01-01 12:00:10");
	}

	SECTION("empty IntermediateStorage round-trip")
	{
		IntermediateStorage empty;
		auto buf = IpcSerializer::serializeIntermediateStorage(empty);
		auto result = IpcSerializer::deserializeIntermediateStorage(buf.data(), buf.size());

		REQUIRE(result != nullptr);
		REQUIRE(result->getStorageNodes().empty());
		REQUIRE(result->getStorageFiles().empty());
		REQUIRE(result->getStorageEdges().empty());
		REQUIRE(result->getErrors().empty());
	}

	SECTION("empty IndexingStatus round-trip")
	{
		IpcSerializer::IndexingStatusData empty;
		auto buf = IpcSerializer::serializeIndexingStatus(empty);
		auto result = IpcSerializer::deserializeIndexingStatus(buf.data(), buf.size());

		REQUIRE(result.indexingFilePaths.empty());
		REQUIRE(result.currentFiles.empty());
		REQUIRE(result.crashedFilePaths.empty());
		REQUIRE(result.finishedProcessIds.empty());
		REQUIRE(result.indexingInterrupted == false);
	}
}
