#include "Catch2.hpp"

#ifndef SRCTRL_MODULE_BUILD
#include "utilityString.h"
#endif

#include <format>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifndef SRCTRL_MODULE_BUILD
#include "Graph.h"
#include "IntermediateStorage.h"
#include "ParserClientImpl.h"
#endif
#include "PersistentStorage.h"
#ifndef SRCTRL_MODULE_BUILD
#include "StorageNodeAttribute.h"
#endif
#include "TestTempDirectory.h"

#include "language_packages.h"

#if BUILD_CXX_LANGUAGE_PACKAGE
#ifndef SRCTRL_MODULE_BUILD
#include "CxxParser.h"
#include "TextAccess.h"
#endif
#include "IndexerStateInfo.h"
#include "TestFileRegister.h"
#include "ToolChain.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
#if BUILD_CXX_LANGUAGE_PACKAGE
import srctrl.cxx;
import srctrl.file;
#endif
import srctrl.indexer;
import srctrl.storage;
import srctrl.utility;
#endif

namespace
{
// Each TestStorage owns its database files in a fresh temp directory (TEST_CASEs run as concurrent
// processes under ctest). TestTempDirectory is the first base, so it is constructed before and
// destroyed after the PersistentStorage that uses it.
class TestStorage: private TestTempDirectory, public PersistentStorage
{
public:
	TestStorage()
		: TestTempDirectory("StorageTestSuite")
		, PersistentStorage(
			  FilePath(TestTempDirectory::getFilePath("test.sqlite")),
			  FilePath(TestTempDirectory::getFilePath("testBookmarks.sqlite")))
	{
		clear();
	}

	// const size_t getNodeCount() const
	//{
	//	return getGraph().getNodeCount();
	//}

	// const size_t getEdgeCount() const
	//{
	//	return getGraph().getEdgeCount();
	//}
};

/*
ParseLocation validLocation(Id locationId = 0)
{
	return ParseLocation(1, 1, locationId, 1, locationId);
}
*/

NameHierarchy createNameHierarchy(std::string s)
{
	NameHierarchy nameHierarchy(NameDelimiterType::CXX);
	for (const std::string &element :
		 utility::splitToVector(s, nameDelimiterTypeToString(NameDelimiterType::CXX))) {
		nameHierarchy.push(element);
	}
	return nameHierarchy;
}

// One translation unit as an indexer records it: its declarations and definitions, and its
// references, through the parser client.
struct RecordedSymbol
{
	std::string name;
	SymbolKind kind;
	size_t line;
};

struct RecordedReference
{
	ReferenceKind kind;
	std::string context;
	std::string referenced;
	size_t line;
};

std::shared_ptr<IntermediateStorage> recordTranslationUnit(
	const std::string& filePath,
	const std::vector<RecordedSymbol>& symbols,
	const std::vector<RecordedReference>& references)
{
	std::shared_ptr<IntermediateStorage> intermediateStorage = std::make_shared<IntermediateStorage>();
	ParserClientImpl client(intermediateStorage);
	const Id fileId = client.recordFile(FilePath(filePath), true);
	for (const RecordedSymbol& symbol: symbols)
	{
		const Id symbolId = client.recordSymbol(createNameHierarchy(symbol.name));
		client.recordSymbolKind(symbolId, symbol.kind);
		client.recordDefinitionKind(symbolId, DefinitionKind::EXPLICIT);
		client.recordLocation(
			symbolId, ParseLocation(fileId, symbol.line, 1, symbol.line, 10), ParseLocationType::TOKEN);
	}
	for (const RecordedReference& reference: references)
	{
		client.recordReference(
			reference.kind,
			client.recordSymbol(createNameHierarchy(reference.referenced)),
			client.recordSymbol(createNameHierarchy(reference.context)),
			ParseLocation(fileId, reference.line, 12, reference.line, 20));
	}
	return intermediateStorage;
}

// Everything the index holds, by name rather than by id, so two indexes built in different orders
// compare equal when they hold the same.
std::set<std::string> describeIndex(const PersistentStorage& storage)
{
	std::map<Id, std::string> names;
	for (const StorageNode& node: storage.getStorageNodes())
	{
		names.emplace(node.id, node.serializedName);
	}
	for (const StorageEdge& edge: storage.getStorageEdges())
	{
		names.emplace(
			edge.id,
			std::format("{} {} -> {}", static_cast<int>(edge.type), names[edge.sourceNodeId], names[edge.targetNodeId]));
	}

	std::set<std::string> lines;
	for (const StorageNode& node: storage.getStorageNodes())
	{
		lines.insert(std::format("node {} {}", static_cast<int>(node.type), node.serializedName));
	}
	for (const StorageEdge& edge: storage.getStorageEdges())
	{
		lines.insert("edge " + names[edge.id]);
	}
	for (const StorageSymbol& symbol: storage.getStorageSymbols())
	{
		lines.insert(std::format("symbol {} {}", names[symbol.id], static_cast<int>(symbol.definitionKind)));
	}
	std::map<Id, StorageSourceLocation> locations;
	for (const StorageSourceLocation& location: storage.getStorageSourceLocations())
	{
		locations.emplace(location.id, location);
	}
	for (const StorageOccurrence& occurrence: storage.getStorageOccurrences())
	{
		const StorageSourceLocation& location = locations[occurrence.sourceLocationId];
		lines.insert(std::format(
			"occurrence {} at {}:{}:{}",
			names[occurrence.elementId],
			names[location.fileNodeId],
			location.startLine,
			location.startCol));
	}
	return lines;
}

bool hasEdge(const PersistentStorage& storage, Edge::EdgeType type, const std::string& source, const std::string& target)
{
	const Id sourceId = storage.getNodeIdForNameHierarchy(createNameHierarchy(source));
	const Id targetId = storage.getNodeIdForNameHierarchy(createNameHierarchy(target));
	for (const StorageEdge& edge: storage.getStorageEdges())
	{
		if (edge.type == type && edge.sourceNodeId == sourceId && edge.targetNodeId == targetId)
		{
			return true;
		}
	}
	return false;
}

/*
NameHierarchy createFunctionNameHierarchy(std::string ret, std::string name, std::string parameters)
{
	NameHierarchy nameHierarchy = createNameHierarchy(name);
	std::string lastName = nameHierarchy.back().getName();
	nameHierarchy.pop();
	nameHierarchy.push(NameElement(lastName, ret, parameters));
	return nameHierarchy;
}
*/

}	 // namespace

TEST_CASE("storage saves file")
{
	TestStorage storage;

	std::string filePath = "path/to/test.h";

	std::shared_ptr<IntermediateStorage> intermediateStorage = std::make_shared<IntermediateStorage>();
	Id id = intermediateStorage->addNode(StorageNodeData(NODE_FILE, NameHierarchy::serialize(NameHierarchy(filePath, NameDelimiterType::FILE)))).first;
	intermediateStorage->addFile(StorageFile(id, filePath, "someLanguage", "someTime", true, true));

	storage.inject(intermediateStorage.get());

	REQUIRE(storage.getNameHierarchyForNodeId(id).getQualifiedName() == filePath);
	REQUIRE(storage.getNodeTypeForNodeWithId(id).isFile());
}

TEST_CASE("storage saves node")
{
	NameHierarchy a = createNameHierarchy("type");

	TestStorage storage;

	std::shared_ptr<IntermediateStorage> intermediateStorage = std::make_shared<IntermediateStorage>();
	intermediateStorage->addNode(StorageNodeData(NODE_TYPEDEF, NameHierarchy::serialize(a)));

	storage.inject(intermediateStorage.get());

	Id storedId = storage.getNodeIdForNameHierarchy(a);

	REQUIRE(storedId != 0);
	REQUIRE(storage.getNodeTypeForNodeWithId(storedId).getKind() == NODE_TYPEDEF);
}

TEST_CASE("storage tooltip surfaces the @available node attribute")
{
	NameHierarchy modern = createNameHierarchy("Modern");

	TestStorage storage;

	std::shared_ptr<IntermediateStorage> intermediateStorage = std::make_shared<IntermediateStorage>();
	Id id = intermediateStorage->addNode(StorageNodeData(NODE_CLASS, NameHierarchy::serialize(modern))).first;
	intermediateStorage->addSymbol(StorageSymbol(id, DefinitionKind::EXPLICIT));
	intermediateStorage->addNodeAttribute(
		StorageNodeAttribute(id, NodeAttributeKind::AVAILABILITY, "macOS 14.0, *"));

	storage.inject(intermediateStorage.get());

	const Id storedId = storage.getNodeIdForNameHierarchy(modern);
	REQUIRE(storedId != 0);

	const TooltipInfo info =
		storage.getTooltipInfoForTokenIds({storedId}, TooltipOrigin::TOOLTIP_ORIGIN_GRAPH);
	REQUIRE(info.title.find("@available(macOS 14.0, *)") != std::string::npos);
}

TEST_CASE("storage tooltip surfaces deprecation (bit + message)")
{
	NameHierarchy old = createNameHierarchy("Old");

	TestStorage storage;

	std::shared_ptr<IntermediateStorage> intermediateStorage = std::make_shared<IntermediateStorage>();
	Id id = intermediateStorage
				->addNode(StorageNodeData(NODE_CLASS, NameHierarchy::serialize(old), NODE_MODIFIER_DEPRECATED))
				.first;
	intermediateStorage->addSymbol(StorageSymbol(id, DefinitionKind::EXPLICIT));
	intermediateStorage->addNodeAttribute(
		StorageNodeAttribute(id, NodeAttributeKind::DEPRECATED, "use New"));

	storage.inject(intermediateStorage.get());

	const Id storedId = storage.getNodeIdForNameHierarchy(old);
	REQUIRE(storedId != 0);

	const TooltipInfo info =
		storage.getTooltipInfoForTokenIds({storedId}, TooltipOrigin::TOOLTIP_ORIGIN_GRAPH);
	REQUIRE(info.title.find("[deprecated: use New]") != std::string::npos);
}

TEST_CASE("storage tooltip surfaces the cfg node attribute")
{
	NameHierarchy gated = createNameHierarchy("Gated");

	TestStorage storage;

	std::shared_ptr<IntermediateStorage> intermediateStorage = std::make_shared<IntermediateStorage>();
	Id id = intermediateStorage->addNode(StorageNodeData(NODE_FUNCTION, NameHierarchy::serialize(gated))).first;
	intermediateStorage->addSymbol(StorageSymbol(id, DefinitionKind::EXPLICIT));
	intermediateStorage->addNodeAttribute(
		StorageNodeAttribute(id, NodeAttributeKind::CFG, "all(unix, feature = \"serde\")"));

	storage.inject(intermediateStorage.get());

	const Id storedId = storage.getNodeIdForNameHierarchy(gated);
	REQUIRE(storedId != 0);

	const TooltipInfo info =
		storage.getTooltipInfoForTokenIds({storedId}, TooltipOrigin::TOOLTIP_ORIGIN_GRAPH);
	REQUIRE(info.title.find("#[cfg(all(unix, feature = \"serde\"))]") != std::string::npos);
}

TEST_CASE("storage saves field as member")
{
	NameHierarchy a = createNameHierarchy("Struct");
	NameHierarchy b = createNameHierarchy("Struct::m_field");

	TestStorage storage;

	std::shared_ptr<IntermediateStorage> intermediateStorage = std::make_shared<IntermediateStorage>();

	Id aId = intermediateStorage->addNode(StorageNodeData(NODE_STRUCT, NameHierarchy::serialize(a))).first;
	intermediateStorage->addSymbol(StorageSymbol(aId, DefinitionKind::EXPLICIT));

	Id bId = intermediateStorage->addNode(StorageNodeData(NODE_FIELD, NameHierarchy::serialize(b))).first;
	intermediateStorage->addSymbol(StorageSymbol(bId, DefinitionKind::EXPLICIT));
	intermediateStorage->addEdge(StorageEdgeData(Edge::EDGE_MEMBER, aId, bId));

	storage.inject(intermediateStorage.get());
	bool foundEdge = false;

	const Id sourceId = storage.getNodeIdForNameHierarchy(a);
	const Id targetId = storage.getNodeIdForNameHierarchy(b);
	for (auto edge: storage.getStorageEdges())
	{
		if (edge.sourceNodeId == sourceId && edge.targetNodeId == targetId &&
			edge.type == Edge::EDGE_MEMBER)
		{
			foundEdge = true;
		}
	}
	REQUIRE(foundEdge);
}

TEST_CASE("storage saves method static")
{
	// TestStorage storage;
	// Id id = storage.onMethodParsed(
	//	validLocation(1),
	//	ParseFunction(typeUsage("void"), createNameHierarchy("isMethod"), parameters("bool"), true),
	//	ParserClient::AccessKind::NONE,
	//	ParserClient::ABSTRACTION_NONE,
	//	validLocation(4)
	//);

	// Node* node = storage.getNodeWithId(id);
	// TS_ASSERT(node);
	// TS_ASSERT_EQUALS(node->getQualifiedNameWithSignature(), "isMethod");
	// TS_ASSERT_EQUALS(node->getType(), NODE_METHOD);
	// TS_ASSERT(node->getComponent<TokenComponentStatic>());
}

TEST_CASE("storage clears single file data of single file storage")
{
	/*
	m_filePath = FilePath("file.cpp");
	TestStorage storage;
	storage.onFunctionParsed(
		validLocation(), ParseFunction(typeUsage("bool"), createNameHierarchy("isTrue"),
		parameters("char")), validLocation()
	);

	REQUIRE(storage.getNodeCount() == 3);
	REQUIRE(storage.getEdgeCount() == 2);
	REQUIRE(storage.tokenLocationCollection().getTokenLocations().size() == 4);

	std::set<FilePath> files;
	files.insert(FilePath(m_filePath));
	storage.clearFileData(files);

	REQUIRE(storage.getNodeCount() == 0);
	REQUIRE(storage.getEdgeCount() == 0);
	REQUIRE(storage.tokenLocationCollection().getTokenLocations().size() == 0);;*/
}

TEST_CASE("storage clears unreferenced single file data of multi file storage")
{
	// m_filePath = "file.h";

	// TestStorage storage;

	// ParseFunction isTrue = ParseFunction(typeUsage("bool"), createNameHierarchy("isTrue"),
	// parameters("char")); storage.onFunctionParsed(validLocation(), isTrue, validLocation());

	// m_filePath = "file.cpp";

	// ParseFunction main = ParseFunction(typeUsage("int"), createNameHierarchy("main"),
	// parameters("void")); storage.onFunctionParsed(validLocation(), main, validLocation());

	// storage.onCallParsed(validLocation(), main, isTrue);

	// TS_ASSERT_EQUALS(storage.getNodeCount(), 6);
	// TS_ASSERT_EQUALS(storage.getEdgeCount(), 5);
	// TS_ASSERT_EQUALS(storage.tokenLocationCollection().getTokenLocations().size(), 9);

	// std::set<FilePath> files;
	// files.insert(FilePath("file.cpp"));
	// storage.clearFileData(files);

	// TS_ASSERT_EQUALS(storage.getNodeCount(), 3);
	// TS_ASSERT_EQUALS(storage.getEdgeCount(), 2);
	// TS_ASSERT_EQUALS(storage.tokenLocationCollection().getTokenLocations().size(), 4);*/
}

TEST_CASE("storage clears referenced single file data of multi file storage")
{
	// m_filePath = "file.h";

	// TestStorage storage;

	// ParseFunction isTrue = ParseFunction(typeUsage("bool"), createNameHierarchy("isTrue"),
	// parameters("void")); storage.onFunctionParsed(validLocation(), isTrue, validLocation());

	// m_filePath = "file.cpp";

	// ParseFunction main = ParseFunction(typeUsage("int"), createNameHierarchy("main"),
	// parameters("void")); storage.onFunctionParsed(validLocation(), main, validLocation());

	// storage.onCallParsed(validLocation(), main, isTrue);

	// TS_ASSERT_EQUALS(storage.getNodeCount(), 5);
	// TS_ASSERT_EQUALS(storage.getEdgeCount(), 5);
	// TS_ASSERT_EQUALS(storage.tokenLocationCollection().getTokenLocations().size(), 9);

	// std::set<FilePath> files;
	// files.insert(FilePath("file.h"));
	// storage.clearFileData(files);

	// TS_ASSERT_EQUALS(storage.getNodeCount(), 4);
	// TS_ASSERT_EQUALS(storage.getEdgeCount(), 3);
	// TS_ASSERT_EQUALS(storage.tokenLocationCollection().getTokenLocations().size(), 5);
}

TEST_CASE("storage clears multi file data of multi file storage")
{
	// m_filePath = "file.h";

	// TestStorage storage;

	// ParseFunction isTrue = ParseFunction(typeUsage("bool"), createNameHierarchy("isTrue"),
	// parameters("void")); storage.onFunctionParsed(validLocation(), isTrue, validLocation());

	// m_filePath = "file.cpp";

	// ParseFunction main = ParseFunction(typeUsage("int"), createNameHierarchy("main"),
	// parameters("void")); storage.onFunctionParsed(validLocation(), main, validLocation());

	// storage.onCallParsed(validLocation(), main, isTrue);

	// TS_ASSERT_EQUALS(storage.getNodeCount(), 5);
	// TS_ASSERT_EQUALS(storage.getEdgeCount(), 5);
	// TS_ASSERT_EQUALS(storage.tokenLocationCollection().getTokenLocations().size(), 9);

	// std::set<FilePath> filePaths;
	// filePaths.insert(FilePath("file.cpp"));
	// filePaths.insert(FilePath("file.h"));
	// storage.clearFileData(filePaths);

	// TS_ASSERT_EQUALS(storage.getNodeCount(), 0);
	// TS_ASSERT_EQUALS(storage.getEdgeCount(), 0);
	// TS_ASSERT_EQUALS(storage.tokenLocationCollection().getTokenLocations().size(), 0);
}

TEST_CASE("storage finds and removes depending file nodes")
{
	// TestStorage storage;

	// Id id1 = storage.onFileParsed("f.h");
	// Id id2 = storage.onFileParsed("file.h");
	// Id id3 = storage.onFileParsed("file.cpp");
	// Id id4 = storage.onFileIncludeParsed(validLocation(), "file.h", "f.h");
	// Id id5 = storage.onFileIncludeParsed(validLocation(), "file.cpp", "file.h");

	// std::string name1 = storage.getNodeWithId(id2)->getQualifiedNameWithSignature();
	// std::string name2 = storage.getNodeWithId(id3)->getQualifiedNameWithSignature();

	// std::set<FilePath> filePaths;
	// filePaths.insert(FilePath(name1));
	// std::set<FilePath> dependingFilePaths = storage.getDependingFilePathsAndRemoveFileNodes(filePaths);

	// TS_ASSERT_EQUALS(dependingFilePaths.size(), 1);
	// TS_ASSERT_EQUALS(dependingFilePaths.begin()->str(), name2);

	// TS_ASSERT(storage.getNodeWithId(id1));
	// TS_ASSERT(!storage.getNodeWithId(id2));
	// TS_ASSERT(!storage.getNodeWithId(id3));
	// TS_ASSERT(!storage.getEdgeWithId(id4));
	// TS_ASSERT(!storage.getEdgeWithId(id5));
}

TEST_CASE("storage boundary graph holds the atoms and what binds to them")
{
	// The graph behind the "boundary" command: a contract atom claimed by two
	// languages, plus every declaration bound to it. See
	// context/DESIGN_XLANG_BOUNDARIES.md and ADR-0009.
	TestStorage storage;

	NameHierarchy atomName(NameDelimiterType::ABI);
	atomName.push("shared_symbol");

	NameHierarchy cxxDeclaration = createNameHierarchy("shared_symbol");
	NameHierarchy rustDeclaration = createNameHierarchy("crate::shared_symbol");
	NameHierarchy singleLanguage = createNameHierarchy("PlainType");

	std::shared_ptr<IntermediateStorage> intermediateStorage = std::make_shared<IntermediateStorage>();

	const Id atomId = intermediateStorage
						  ->addNode(StorageNodeData(
							  NODE_SYMBOL,
							  NameHierarchy::serialize(atomName),
							  NODE_MODIFIER_NONE,
							  LANGUAGE_CXX | LANGUAGE_RUST))
						  .first;
	const Id cxxId = intermediateStorage
						 ->addNode(StorageNodeData(
							 NODE_FUNCTION,
							 NameHierarchy::serialize(cxxDeclaration),
							 NODE_MODIFIER_NONE,
							 LANGUAGE_CXX))
						 .first;
	const Id rustId = intermediateStorage
						  ->addNode(StorageNodeData(
							  NODE_FUNCTION,
							  NameHierarchy::serialize(rustDeclaration),
							  NODE_MODIFIER_NONE,
							  LANGUAGE_RUST))
						  .first;
	// Claimed by one language only: not a boundary, must stay out.
	const Id plainId = intermediateStorage
						   ->addNode(StorageNodeData(
							   NODE_CLASS,
							   NameHierarchy::serialize(singleLanguage),
							   NODE_MODIFIER_NONE,
							   LANGUAGE_CXX))
						   .first;
	// A file two indexers named is a deliberate identity merge, not a boundary,
	// so it is excluded even though its mask has two bits set.
	const std::string sharedPath = "path/to/shared.h";
	const Id fileId = intermediateStorage
						  ->addNode(StorageNodeData(
							  NODE_FILE,
							  NameHierarchy::serialize(
								  NameHierarchy(sharedPath, NameDelimiterType::FILE)),
							  NODE_MODIFIER_NONE,
							  LANGUAGE_CXX | LANGUAGE_ZIG))
						  .first;
	intermediateStorage->addFile(StorageFile(fileId, sharedPath, "cpp", "someTime", true, true));

	intermediateStorage->addEdge(StorageEdgeData(Edge::EDGE_BINDS, cxxId, atomId));
	intermediateStorage->addEdge(StorageEdgeData(Edge::EDGE_BINDS, rustId, atomId));

	storage.inject(intermediateStorage.get());
	storage.buildCaches();

	std::shared_ptr<Graph> graph = storage.getGraphForLanguageBoundaries();

	const Id storedAtomId = storage.getNodeIdForNameHierarchy(atomName);
	const Id storedCxxId = storage.getNodeIdForNameHierarchy(cxxDeclaration);
	const Id storedRustId = storage.getNodeIdForNameHierarchy(rustDeclaration);

	REQUIRE(graph->getNodeById(storedAtomId) != nullptr);
	// Both binders come along: an atom on its own says nothing, the pairing is
	// the answer.
	REQUIRE(graph->getNodeById(storedCxxId) != nullptr);
	REQUIRE(graph->getNodeById(storedRustId) != nullptr);

	REQUIRE(graph->getNodeById(storage.getNodeIdForNameHierarchy(singleLanguage)) == nullptr);
	REQUIRE(graph->getNodeById(storage.getNodeIdForFileNode(FilePath(sharedPath))) == nullptr);

	// The mask survives into the graph, which is what draws the heavy border.
	REQUIRE(graph->getNodeById(storedAtomId)->isLanguageBoundary());
	REQUIRE(!graph->getNodeById(storedCxxId)->isLanguageBoundary());
}

TEST_CASE("storage refresh of one file leaves the index a full index would have")
{
	// b.cc is not cleared. It declares ns::B, defines ns::write, which calls ns::doWrite, and
	// takes a ns::B, as the declaration in a.cc does.
	const std::shared_ptr<IntermediateStorage> b = recordTranslationUnit(
		"b.cc",
		{{"ns", SymbolKind::NAMESPACE, 1},
		 {"ns::B", SymbolKind::STRUCT, 2},
		 {"ns::write", SymbolKind::FUNCTION, 3},
		 {"ns::doWrite", SymbolKind::FUNCTION, 4}},
		{{ReferenceKind::CALL, "ns::write", "ns::doWrite", 5},
		 {ReferenceKind::TYPE_USAGE, "ns::write", "ns::B", 3}});

	// a.cc before the change: it forward-declares ns::write, declares ns::A and ns::Gone with a
	// member, and calls outer::inner::leaf, which is declared in no indexed file.
	const std::shared_ptr<IntermediateStorage> aBefore = recordTranslationUnit(
		"a.cc",
		{{"ns", SymbolKind::NAMESPACE, 1},
		 {"ns::A", SymbolKind::STRUCT, 2},
		 {"ns::write", SymbolKind::FUNCTION, 3},
		 {"ns::Gone", SymbolKind::STRUCT, 4},
		 {"ns::Gone::member", SymbolKind::FIELD, 5},
		 {"ns::run", SymbolKind::FUNCTION, 6}},
		{{ReferenceKind::TYPE_USAGE, "ns::write", "ns::B", 3},
		 {ReferenceKind::TYPE_USAGE, "ns::A", "ns::B", 2},
		 {ReferenceKind::CALL, "ns::run", "outer::inner::leaf", 7}});

	// a.cc after the change: ns::Gone and the call to outer::inner::leaf are gone, ns::Added is new.
	const std::shared_ptr<IntermediateStorage> aAfter = recordTranslationUnit(
		"a.cc",
		{{"ns", SymbolKind::NAMESPACE, 1},
		 {"ns::A", SymbolKind::STRUCT, 2},
		 {"ns::write", SymbolKind::FUNCTION, 3},
		 {"ns::Added", SymbolKind::STRUCT, 4},
		 {"ns::run", SymbolKind::FUNCTION, 6}},
		{{ReferenceKind::TYPE_USAGE, "ns::write", "ns::B", 3},
		 {ReferenceKind::TYPE_USAGE, "ns::A", "ns::B", 2}});

	TestStorage full;
	full.inject(b.get());
	full.inject(aAfter.get());

	TestStorage refreshed;
	refreshed.inject(aBefore.get());
	refreshed.inject(b.get());
	REQUIRE(hasEdge(refreshed, Edge::EDGE_CALL, "ns::run", "outer::inner::leaf"));

	// as a refresh does (TaskCleanStorage, then the injection of the reindexed files)
	refreshed.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_CLEAR);
	refreshed.clearFileElements({FilePath("a.cc")}, [](int) {});

	// What b.cc recorded stays: the namespace's member edge to ns::B, which b.cc recorded although
	// ns occurs in a.cc as well, the call located in b.cc, although a.cc declares the caller, and
	// the type usage in b.cc, although a.cc has the same.
	REQUIRE(hasEdge(refreshed, Edge::EDGE_MEMBER, "ns", "ns::B"));
	REQUIRE(hasEdge(refreshed, Edge::EDGE_MEMBER, "ns", "ns::write"));
	REQUIRE(hasEdge(refreshed, Edge::EDGE_CALL, "ns::write", "ns::doWrite"));
	REQUIRE(hasEdge(refreshed, Edge::EDGE_TYPE_USAGE, "ns::write", "ns::B"));
	// What only a.cc recorded goes, with the nodes nothing else names.
	REQUIRE(!hasEdge(refreshed, Edge::EDGE_TYPE_USAGE, "ns::A", "ns::B"));
	REQUIRE(refreshed.getNodeIdForNameHierarchy(createNameHierarchy("ns::Gone::member")) == 0);
	REQUIRE(refreshed.getNodeIdForNameHierarchy(createNameHierarchy("ns::Gone")) == 0);
	REQUIRE(refreshed.getNodeIdForNameHierarchy(createNameHierarchy("outer::inner::leaf")) == 0);
	REQUIRE(refreshed.getNodeIdForNameHierarchy(createNameHierarchy("outer")) == 0);

	refreshed.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_WRITE);
	refreshed.inject(aAfter.get());

	REQUIRE(describeIndex(refreshed) == describeIndex(full));
}

TEST_CASE("storage refresh of every file but one leaves the index a full index would have")
{
	// The other way around: the file that recorded the namespace's members is the one cleared.
	const std::shared_ptr<IntermediateStorage> header = recordTranslationUnit(
		"db.h",
		{{"leveldb", SymbolKind::NAMESPACE, 1},
		 {"leveldb::DB", SymbolKind::CLASS, 2},
		 {"leveldb::DB::Open", SymbolKind::METHOD, 3}},
		{});
	const std::shared_ptr<IntermediateStorage> source = recordTranslationUnit(
		"hash.cc",
		{{"leveldb", SymbolKind::NAMESPACE, 1}, {"leveldb::Hash", SymbolKind::FUNCTION, 2}},
		{{ReferenceKind::CALL, "leveldb::Hash", "leveldb::DB::Open", 3}});

	TestStorage full;
	full.inject(header.get());
	full.inject(source.get());

	TestStorage refreshed;
	refreshed.inject(header.get());
	refreshed.inject(source.get());
	refreshed.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_CLEAR);
	refreshed.clearFileElements({FilePath("db.h")}, [](int) {});

	// leveldb::DB::Open is still called from hash.cc, so it and its parents stay, with their
	// member edges.
	REQUIRE(hasEdge(refreshed, Edge::EDGE_MEMBER, "leveldb", "leveldb::DB"));
	REQUIRE(hasEdge(refreshed, Edge::EDGE_MEMBER, "leveldb::DB", "leveldb::DB::Open"));
	REQUIRE(hasEdge(refreshed, Edge::EDGE_MEMBER, "leveldb", "leveldb::Hash"));

	refreshed.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_WRITE);
	refreshed.inject(header.get());

	REQUIRE(describeIndex(refreshed) == describeIndex(full));
}

#if BUILD_CXX_LANGUAGE_PACKAGE
namespace
{
std::shared_ptr<IntermediateStorage> parseFile(const std::string& filePath, const std::string& code)
{
	std::shared_ptr<IntermediateStorage> intermediateStorage = std::make_shared<IntermediateStorage>();
	ParserClientImpl client(intermediateStorage);
	CxxParser parser(client, std::make_shared<TestFileRegister>(), std::make_shared<IndexerStateInfo>());
	parser.buildIndex(
		filePath,
		TextAccess::createFromString(code),
		std::vector<std::string>{ClangCompiler::stdOption(ClangCompiler::getLatestCppStandard())});
	return intermediateStorage;
}
}	 // namespace

TEST_CASE("storage refresh of one parsed file leaves the index a full index would have")
{
	// leveldb's db/filename.cc forward-declares WriteStringToFileSync, which util/env.cc defines;
	// clearing filename.cc deleted the call in env.cc and the namespace's member edges to all
	// that env.cc declares.
	const std::shared_ptr<IntermediateStorage> env = parseFile(
		"env.cc",
		"namespace leveldb {\n"
		"struct Env {};\n"
		"static int DoWriteStringToFile(Env* env) { return 0; }\n"
		"int WriteStringToFileSync(Env* env) { return DoWriteStringToFile(env); }\n"
		"}\n");
	const std::shared_ptr<IntermediateStorage> filenameBefore = parseFile(
		"filename.cc",
		"namespace leveldb {\n"
		"struct Env;\n"
		"int WriteStringToFileSync(Env* env);\n"
		"struct Gone { int member; };\n"
		"int SetCurrentFile(Env* env) { return WriteStringToFileSync(env); }\n"
		"}\n");
	const std::shared_ptr<IntermediateStorage> filenameAfter = parseFile(
		"filename.cc",
		"namespace leveldb {\n"
		"struct Env;\n"
		"int WriteStringToFileSync(Env* env);\n"
		"struct Added {};\n"
		"int SetCurrentFile(Env* env) { return WriteStringToFileSync(env); }\n"
		"}\n");

	TestStorage full;
	full.inject(env.get());
	full.inject(filenameAfter.get());

	TestStorage refreshed;
	refreshed.inject(filenameBefore.get());
	refreshed.inject(env.get());

	refreshed.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_CLEAR);
	refreshed.clearFileElements({FilePath("filename.cc")}, [](int) {});

	REQUIRE(hasEdge(refreshed, Edge::EDGE_MEMBER, "leveldb", "leveldb::Env"));
	REQUIRE(refreshed.getNodeIdForNameHierarchy(createNameHierarchy("leveldb::Gone")) == 0);

	refreshed.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_WRITE);
	refreshed.inject(filenameAfter.get());

	REQUIRE(describeIndex(refreshed) == describeIndex(full));
}
#endif
