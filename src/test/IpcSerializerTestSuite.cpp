#include "Catch2.hpp"
// Id is a classic (non-exported) header: textual, GM everywhere (rule 5).
#include "Id.h"

#include "language_package_flags.h"

#ifndef SRCTRL_MODULE_BUILD
#include "IndexerCommandSerializer.h"
#include "IntermediateStorageSerializer.h"
#include "IndexingStatusSerializer.h"
#include "GarbageCollectorSerializer.h"
#endif
#include "OptionalCxxTestUtils.h"
#include "OptionalRustTestUtils.h"
#include "OptionalSwiftTestUtils.h"

#ifndef SRCTRL_MODULE_BUILD
#include "IntermediateStorage.h"
#include "IndexerCommand.h"
#include "IndexerCommandSwift.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
// Id/Edge/DefinitionKind came transitively before; visibility does not flow through imports.
import srctrl.data;
import srctrl.indexer;
import srctrl.interprocess;
import srctrl.storage;
#endif

TEST_CASE("ipc serializer round-trips")
{
	SECTION("IndexerCommand CXX round-trip")
	{
		if constexpr (language_packages::buildCxxLanguagePackage)
			assertOptionalCxxSerializerRoundTrip();
		else
			SUCCEED("CXX language package disabled.");
	}

	SECTION("IndexerCommand Rust round-trip")
	{
		if constexpr (language_packages::buildRustLanguagePackage)
			assertOptionalRustSerializerRoundTrip();
		else
			SUCCEED("Rust language package disabled.");
	}

	SECTION("IndexerCommand Swift round-trip")
	{
		if constexpr (language_packages::buildSwiftLanguagePackage)
			assertOptionalSwiftSerializerRoundTrip();
		else
			SUCCEED("Swift language package disabled.");
	}

	SECTION("IndexerCommand source-group id round-trip")
	{
		// The group tag (fan-out S1) rides every command type through the base
		// class; Swift commands are compiled unconditionally, so use one.
		auto tagged = std::make_shared<IndexerCommand>(FilePath("/src/main.swift"), IndexerCommandSwift(std::set<FilePath>{FilePath("/src")}, FilePath("/src")));
		tagged->setSourceGroupId("6f0d4b2e-9c1a-4a5e-8f00-1234567890ab");
		auto untagged = std::make_shared<IndexerCommand>(FilePath("/src/other.swift"), IndexerCommandSwift(std::set<FilePath>{}, FilePath("/src")));

		auto buf = IpcSerializer::serializeIndexerCommands({tagged, untagged});
		auto result = IpcSerializer::deserializeIndexerCommands(buf.data(), buf.size());

		REQUIRE(result.size() == 2);
		REQUIRE(result[0]->getSourceGroupId() == "6f0d4b2e-9c1a-4a5e-8f00-1234567890ab");
		REQUIRE(result[1]->getSourceGroupId().empty());
	}

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
		original.addNodeAttribute(
			StorageNodeAttribute(Id(1), NodeAttributeKind::AVAILABILITY, "@available(macOS 14, *)"));
		original.addNodeAttribute(
			StorageNodeAttribute(Id(2), NodeAttributeKind::DEPRECATED, "use doSomethingElse"));
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
		REQUIRE(result->getNodeAttributes().size() == 2);
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

		const auto& nodeAttributes = result->getNodeAttributes();
		const StorageNodeAttribute& firstAttribute = *nodeAttributes.begin();
		REQUIRE(firstAttribute.nodeId == Id(1));
		REQUIRE(firstAttribute.key == NodeAttributeKind::AVAILABILITY);
		REQUIRE(firstAttribute.value == "@available(macOS 14, *)");

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
