#include "Catch2.hpp"

#ifndef SRCTRL_MODULE_BUILD
#include "utilityString.h"
#endif

#include <algorithm>
#include <expected>
#include <format>
#include <map>
#include <optional>
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
#include "TokenComponentInstantiations.h"
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
	// IMPLICIT for an implicit template instantiation
	DefinitionKind definitionKind = DefinitionKind::EXPLICIT;
};

struct RecordedReference
{
	ReferenceKind kind;
	std::string context;
	std::string referenced;
	size_t line;
	// false: a use in implicit code or in a macro, which the indexer records without a location
	bool located = true;
	// of a call: whether it dispatches, as the C++ indexer records it; unset for an indexer that
	// does not tell
	std::optional<bool> dispatches = std::nullopt;
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
		client.recordDefinitionKind(symbolId, symbol.definitionKind);
		client.recordLocation(
			symbolId, ParseLocation(fileId, symbol.line, 1, symbol.line, 10), ParseLocationType::TOKEN);
	}
	for (const RecordedReference& reference: references)
	{
		const Id edgeId = client.recordReference(
			reference.kind,
			client.recordSymbol(createNameHierarchy(reference.referenced)),
			client.recordSymbol(createNameHierarchy(reference.context)),
			reference.located ? ParseLocation(fileId, reference.line, 12, reference.line, 20) : ParseLocation());
		if (reference.dispatches)
		{
			client.recordCallDispatch(edgeId, *reference.dispatches);
		}
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
		 {ReferenceKind::TYPE_USAGE, "ns::write", "ns::B", 3},
		 {ReferenceKind::TYPE_USAGE, "ns::doWrite", "ns::Implicit", 4, false}});

	// a.cc before the change: it forward-declares ns::write, declares ns::A and ns::Gone with a
	// member, and calls outer::inner::leaf, which is declared in no indexed file. ns::run uses
	// ns::GoneImplicit in implicit code (a template instantiation), and a macro defines a variable
	// that uses itself, neither with a location (doctest's DOCTEST_ANON_VAR_25).
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
		 {ReferenceKind::CALL, "ns::run", "outer::inner::leaf", 7},
		 {ReferenceKind::TYPE_USAGE, "ns::run", "ns::GoneImplicit", 6, false},
		 {ReferenceKind::USAGE, "ANON_VAR_8", "ANON_VAR_8", 8, false}});

	// a.cc after the change: ns::Gone, the call to outer::inner::leaf and the uses without a
	// location are gone, ns::Added is new, and ns::run uses ns::Implicit in implicit code.
	const std::shared_ptr<IntermediateStorage> aAfter = recordTranslationUnit(
		"a.cc",
		{{"ns", SymbolKind::NAMESPACE, 1},
		 {"ns::A", SymbolKind::STRUCT, 2},
		 {"ns::write", SymbolKind::FUNCTION, 3},
		 {"ns::Added", SymbolKind::STRUCT, 4},
		 {"ns::run", SymbolKind::FUNCTION, 6}},
		{{ReferenceKind::TYPE_USAGE, "ns::write", "ns::B", 3},
		 {ReferenceKind::TYPE_USAGE, "ns::A", "ns::B", 2},
		 {ReferenceKind::TYPE_USAGE, "ns::run", "ns::Implicit", 6, false}});

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
	REQUIRE(hasEdge(refreshed, Edge::EDGE_TYPE_USAGE, "ns::doWrite", "ns::Implicit"));
	// What only a.cc recorded goes, with the nodes nothing else names, also without locations.
	REQUIRE(!hasEdge(refreshed, Edge::EDGE_TYPE_USAGE, "ns::run", "ns::GoneImplicit"));
	REQUIRE(refreshed.getNodeIdForNameHierarchy(createNameHierarchy("ns::GoneImplicit")) == 0);
	REQUIRE(refreshed.getNodeIdForNameHierarchy(createNameHierarchy("ANON_VAR_8")) == 0);
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

namespace
{
// The dispatch edges of `storage`, as "caller -> override".
std::set<std::string> dispatchEdges(const PersistentStorage& storage)
{
	std::map<Id, std::string> names;
	for (const StorageNode& node: storage.getStorageNodes())
	{
		names.emplace(node.id, NameHierarchy::deserialize(node.serializedName).getQualifiedName());
	}
	std::set<std::string> edges;
	for (const StorageEdge& edge: storage.getStorageEdges())
	{
		if (edge.type == Edge::EDGE_DISPATCH)
		{
			edges.insert(names[edge.sourceNodeId] + " -> " + names[edge.targetNodeId]);
		}
	}
	return edges;
}

std::string dispatchEdge(const std::string& caller, const std::string& override)
{
	return caller + " -> " + override;
}

// The lines the occurrences of the dispatch edge from `caller` to `override` are on.
std::set<size_t> dispatchEdgeLines(
	const PersistentStorage& storage, const std::string& caller, const std::string& override)
{
	std::map<Id, std::string> names;
	for (const StorageNode& node: storage.getStorageNodes())
	{
		names.emplace(node.id, NameHierarchy::deserialize(node.serializedName).getQualifiedName());
	}
	std::set<Id> edgeIds;
	for (const StorageEdge& edge: storage.getStorageEdges())
	{
		if (edge.type == Edge::EDGE_DISPATCH && names[edge.sourceNodeId] == caller &&
			names[edge.targetNodeId] == override)
		{
			edgeIds.insert(edge.id);
		}
	}
	std::map<Id, size_t> lines;
	for (const StorageSourceLocation& location: storage.getStorageSourceLocations())
	{
		lines.emplace(location.id, location.startLine);
	}
	std::set<size_t> result;
	for (const StorageOccurrence& occurrence: storage.getStorageOccurrences())
	{
		if (edgeIds.contains(occurrence.elementId))
		{
			result.insert(lines[occurrence.sourceLocationId]);
		}
	}
	return result;
}

// A hierarchy with overrides two levels deep, a class that inherits a method without overriding
// it, and a diamond, as one translation unit records it; and callers through each level.
std::shared_ptr<IntermediateStorage> recordShapes()
{
	return recordTranslationUnit(
		"shapes.cc",
		{{"Shape", SymbolKind::CLASS, 1},
		 {"Shape::draw", SymbolKind::METHOD, 2},
		 {"Circle", SymbolKind::CLASS, 3},
		 {"Circle::draw", SymbolKind::METHOD, 4},
		 {"Wheel", SymbolKind::CLASS, 5},
		 {"Wheel::draw", SymbolKind::METHOD, 6},
		 {"Square", SymbolKind::CLASS, 7},
		 {"A", SymbolKind::CLASS, 10},
		 {"A::f", SymbolKind::METHOD, 11},
		 {"B", SymbolKind::CLASS, 12},
		 {"B::f", SymbolKind::METHOD, 13},
		 {"C", SymbolKind::CLASS, 14},
		 {"C::f", SymbolKind::METHOD, 15},
		 {"D", SymbolKind::CLASS, 16},
		 {"D::f", SymbolKind::METHOD, 17},
		 {"render", SymbolKind::FUNCTION, 20},
		 {"renderCircle", SymbolKind::FUNCTION, 22},
		 {"renderBoth", SymbolKind::FUNCTION, 24},
		 {"useA", SymbolKind::FUNCTION, 27}},
		{{ReferenceKind::INHERITANCE, "Circle", "Shape", 3},
		 {ReferenceKind::INHERITANCE, "Wheel", "Circle", 5},
		 {ReferenceKind::INHERITANCE, "Square", "Shape", 7},
		 {ReferenceKind::OVERRIDE, "Circle::draw", "Shape::draw", 4},
		 {ReferenceKind::OVERRIDE, "Wheel::draw", "Circle::draw", 6},
		 // Wheel::draw calls Circle::draw, the implementation it overrides: Circle::draw().
		 {ReferenceKind::CALL, "Wheel::draw", "Circle::draw", 6},
		 {ReferenceKind::INHERITANCE, "B", "A", 12},
		 {ReferenceKind::INHERITANCE, "C", "A", 14},
		 {ReferenceKind::INHERITANCE, "D", "B", 16},
		 {ReferenceKind::INHERITANCE, "D", "C", 16},
		 {ReferenceKind::OVERRIDE, "B::f", "A::f", 13},
		 {ReferenceKind::OVERRIDE, "C::f", "A::f", 15},
		 {ReferenceKind::OVERRIDE, "D::f", "B::f", 17},
		 {ReferenceKind::OVERRIDE, "D::f", "C::f", 17},
		 {ReferenceKind::CALL, "render", "Shape::draw", 21},
		 {ReferenceKind::CALL, "renderCircle", "Circle::draw", 23},
		 {ReferenceKind::CALL, "renderBoth", "Shape::draw", 25},
		 {ReferenceKind::CALL, "renderBoth", "Circle::draw", 26},
		 {ReferenceKind::CALL, "useA", "A::f", 28}});
}
}	 // namespace

TEST_CASE("storage derives the overrides a call through a base method may reach")
{
	TestStorage storage;
	storage.inject(recordShapes().get());
	storage.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);

	const std::expected<std::size_t, DerivedEdgeError> derived = storage.deriveDispatchEdges();
	REQUIRE(derived);

	const std::set<std::string> expected {
		// every override below the called method, at any depth; not the method called, and not
		// Square, which overrides nothing
		dispatchEdge("render", "Circle::draw"),
		dispatchEdge("render", "Wheel::draw"),
		// a call through the middle of the hierarchy reaches only what is below it
		dispatchEdge("renderCircle", "Wheel::draw"),
		// renderBoth calls Circle::draw itself: only Wheel::draw is derived, once for both calls
		dispatchEdge("renderBoth", "Wheel::draw"),
		// D::f overrides A::f twice over, through B and through C: one edge
		dispatchEdge("useA", "B::f"),
		dispatchEdge("useA", "C::f"),
		dispatchEdge("useA", "D::f"),
		// Wheel::draw calling Circle::draw, which it overrides, is a call of the base
		// implementation: nothing is derived from it
	};
	REQUIRE(dispatchEdges(storage) == expected);
	REQUIRE(*derived == expected.size());

	// A derived edge occurs where the calls it comes from do: renderBoth reaches Wheel::draw
	// through both of its calls.
	REQUIRE(dispatchEdgeLines(storage, "render", "Wheel::draw") == std::set<size_t> {21});
	REQUIRE(dispatchEdgeLines(storage, "renderBoth", "Wheel::draw") == std::set<size_t> {25, 26});

	// The calls themselves are as they were.
	REQUIRE(hasEdge(storage, Edge::EDGE_CALL, "render", "Shape::draw"));
	REQUIRE(!hasEdge(storage, Edge::EDGE_CALL, "render", "Circle::draw"));
}

TEST_CASE("storage derives the same dispatch edges when it derives them again")
{
	TestStorage storage;
	storage.inject(recordShapes().get());
	storage.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);

	REQUIRE(storage.deriveDispatchEdges());
	const std::set<std::string> once = describeIndex(storage);
	const std::size_t edgeCount = storage.getStorageEdges().size();

	const std::expected<std::size_t, DerivedEdgeError> again = storage.deriveDispatchEdges();
	REQUIRE(again);
	REQUIRE(*again == dispatchEdges(storage).size());
	REQUIRE(describeIndex(storage) == once);
	REQUIRE(storage.getStorageEdges().size() == edgeCount);
}

TEST_CASE("storage derivation ends on an override cycle")
{
	// Only a name collision makes one (two classes of the same name in two programs), but the
	// recursion has to end all the same.
	TestStorage storage;
	storage.inject(recordTranslationUnit(
					   "cycle.cc",
					   {{"X", SymbolKind::CLASS, 1},
						{"X::g", SymbolKind::METHOD, 2},
						{"Y", SymbolKind::CLASS, 3},
						{"Y::g", SymbolKind::METHOD, 4},
						{"call", SymbolKind::FUNCTION, 5}},
					   {{ReferenceKind::OVERRIDE, "Y::g", "X::g", 4},
						{ReferenceKind::OVERRIDE, "X::g", "Y::g", 2},
						{ReferenceKind::CALL, "call", "X::g", 6}})
					   .get());
	storage.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);

	REQUIRE(storage.deriveDispatchEdges());
	REQUIRE(dispatchEdges(storage) == std::set<std::string> {dispatchEdge("call", "Y::g")});
}

TEST_CASE("storage refresh derives the dispatch edges a full index would have")
{
	// shape.h declares the base, render.cc calls through it, circle.cc overrides it. After the
	// change circle.cc no longer overrides Shape::draw, and oval.cc, new, does.
	const std::shared_ptr<IntermediateStorage> shape = recordTranslationUnit(
		"shape.h", {{"Shape", SymbolKind::CLASS, 1}, {"Shape::draw", SymbolKind::METHOD, 2}}, {});
	const std::shared_ptr<IntermediateStorage> render = recordTranslationUnit(
		"render.cc",
		{{"render", SymbolKind::FUNCTION, 1}},
		{{ReferenceKind::CALL, "render", "Shape::draw", 2}});
	const std::shared_ptr<IntermediateStorage> circleBefore = recordTranslationUnit(
		"circle.cc",
		{{"Circle", SymbolKind::CLASS, 1}, {"Circle::draw", SymbolKind::METHOD, 2}},
		{{ReferenceKind::INHERITANCE, "Circle", "Shape", 1},
		 {ReferenceKind::OVERRIDE, "Circle::draw", "Shape::draw", 2}});
	const std::shared_ptr<IntermediateStorage> circleAfter = recordTranslationUnit(
		"circle.cc", {{"Circle", SymbolKind::CLASS, 1}, {"Circle::area", SymbolKind::METHOD, 2}}, {});
	const std::shared_ptr<IntermediateStorage> oval = recordTranslationUnit(
		"oval.cc",
		{{"Oval", SymbolKind::CLASS, 1}, {"Oval::draw", SymbolKind::METHOD, 2}},
		{{ReferenceKind::INHERITANCE, "Oval", "Shape", 1},
		 {ReferenceKind::OVERRIDE, "Oval::draw", "Shape::draw", 2}});

	TestStorage full;
	full.inject(shape.get());
	full.inject(render.get());
	full.inject(circleAfter.get());
	full.inject(oval.get());
	full.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);
	REQUIRE(full.deriveDispatchEdges());
	REQUIRE(dispatchEdges(full) == std::set<std::string> {dispatchEdge("render", "Oval::draw")});

	TestStorage refreshed;
	refreshed.inject(shape.get());
	refreshed.inject(render.get());
	refreshed.inject(circleBefore.get());
	refreshed.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);
	REQUIRE(refreshed.deriveDispatchEdges());
	REQUIRE(dispatchEdges(refreshed) == std::set<std::string> {dispatchEdge("render", "Circle::draw")});

	// as a refresh does: clear, inject the files indexed again, then derive over everything
	refreshed.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_CLEAR);
	refreshed.clearFileElements({FilePath("circle.cc")}, [](int) {});
	REQUIRE(dispatchEdges(refreshed).empty());
	// Circle::draw only had the derived edge besides what circle.cc recorded: it goes.
	REQUIRE(refreshed.getNodeIdForNameHierarchy(createNameHierarchy("Circle::draw")) == 0);

	refreshed.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_WRITE);
	refreshed.inject(circleAfter.get());
	refreshed.inject(oval.get());
	refreshed.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);
	REQUIRE(refreshed.deriveDispatchEdges());

	REQUIRE(describeIndex(refreshed) == describeIndex(full));
}

namespace
{
// A composite: Node::visit is overridden by Leaf and Group, and Group::visit forwards to its
// children through Node::visit -- a call from an override to the method it overrides that does
// dispatch. render calls through Node; `dispatches` is what the indexer said of each, if anything.
std::shared_ptr<IntermediateStorage> recordComposite(
	const std::string& filePath,
	std::optional<bool> groupDispatches,
	std::optional<bool> renderDispatches)
{
	return recordTranslationUnit(
		filePath,
		{{"Node", SymbolKind::CLASS, 1},
		 {"Node::visit", SymbolKind::METHOD, 2},
		 {"Leaf", SymbolKind::CLASS, 3},
		 {"Leaf::visit", SymbolKind::METHOD, 4},
		 {"Group", SymbolKind::CLASS, 5},
		 {"Group::visit", SymbolKind::METHOD, 6},
		 {"render", SymbolKind::FUNCTION, 8}},
		{{ReferenceKind::INHERITANCE, "Leaf", "Node", 3},
		 {ReferenceKind::INHERITANCE, "Group", "Node", 5},
		 {ReferenceKind::OVERRIDE, "Leaf::visit", "Node::visit", 4},
		 {ReferenceKind::OVERRIDE, "Group::visit", "Node::visit", 6},
		 {ReferenceKind::CALL, "Group::visit", "Node::visit", 7, true, groupDispatches},
		 {ReferenceKind::CALL, "render", "Node::visit", 9, true, renderDispatches}});
}

std::size_t countCallDispatchComponents(const PersistentStorage& storage)
{
	return static_cast<std::size_t>(std::ranges::count_if(
		storage.getElementComponents(), [](const StorageElementComponent& component) {
			return component.type == ElementComponentKind::ELEMENT_COMPONENT_CALL_DISPATCHES ||
				component.type == ElementComponentKind::ELEMENT_COMPONENT_CALL_STATIC;
		}));
}
}	 // namespace

TEST_CASE("storage derives from what the indexer says a call does, and guesses only when it does not say")
{
	SECTION("not said: a call from an override to the method it overrides is taken for a super call")
	{
		TestStorage storage;
		storage.inject(recordComposite("a.cc", std::nullopt, std::nullopt).get());
		storage.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);
		REQUIRE(storage.deriveDispatchEdges());
		REQUIRE(
			dispatchEdges(storage) ==
			std::set<std::string> {dispatchEdge("render", "Leaf::visit"), dispatchEdge("render", "Group::visit")});
	}

	SECTION("said to dispatch: the forwarding override gets its edges, to itself included")
	{
		TestStorage storage;
		storage.inject(recordComposite("a.cc", true, true).get());
		storage.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);
		REQUIRE(storage.deriveDispatchEdges());
		REQUIRE(
			dispatchEdges(storage) ==
			std::set<std::string> {
				dispatchEdge("render", "Leaf::visit"),
				dispatchEdge("render", "Group::visit"),
				dispatchEdge("Group::visit", "Leaf::visit"),
				dispatchEdge("Group::visit", "Group::visit")});
	}

	SECTION("said not to dispatch: nothing, whoever calls")
	{
		TestStorage storage;
		storage.inject(recordComposite("a.cc", false, false).get());
		storage.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);
		REQUIRE(storage.deriveDispatchEdges());
		REQUIRE(dispatchEdges(storage).empty());
	}
}

TEST_CASE("storage ors what the translation units say of a call")
{
	// A header's inline function is indexed by every translation unit that includes it, and one
	// edge holds all of a caller's calls of a method: it dispatches if any of them does.
	for (const bool dispatchingFirst: {true, false})
	{
		TestStorage storage;
		const std::shared_ptr<IntermediateStorage> dispatching = recordComposite("a.cc", true, true);
		const std::shared_ptr<IntermediateStorage> bound = recordComposite("b.cc", false, false);
		storage.inject((dispatchingFirst ? dispatching : bound).get());
		storage.inject((dispatchingFirst ? bound : dispatching).get());
		// and once more, as another translation unit would: kept once per edge and kind
		storage.inject(bound.get());
		storage.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);
		REQUIRE(storage.deriveDispatchEdges());

		REQUIRE(dispatchEdges(storage).contains(dispatchEdge("Group::visit", "Leaf::visit")));
		REQUIRE(dispatchEdges(storage).contains(dispatchEdge("render", "Leaf::visit")));
		// two call edges, each said to dispatch and said not to
		REQUIRE(countCallDispatchComponents(storage) == 4);
	}

	SECTION("and so does merging indexes, as the merge command does")
	{
		TestStorage shardA;
		shardA.inject(recordComposite("a.cc", false, false).get());
		TestStorage shardB;
		shardB.inject(recordComposite("b.cc", true, false).get());

		TestStorage merged;
		merged.inject(&shardA);
		merged.inject(&shardB);
		merged.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);
		REQUIRE(merged.deriveDispatchEdges());
		REQUIRE(
			dispatchEdges(merged) ==
			std::set<std::string> {
				dispatchEdge("Group::visit", "Leaf::visit"), dispatchEdge("Group::visit", "Group::visit")});
	}
}

TEST_CASE("storage refresh forgets what the cleared file said of its calls")
{
	// a.cc's render called through Node::visit on a pointer; after the change it calls it on an
	// object. node.h declares the hierarchy and is not cleared.
	const std::shared_ptr<IntermediateStorage> header = recordTranslationUnit(
		"node.h",
		{{"Node", SymbolKind::CLASS, 1},
		 {"Node::visit", SymbolKind::METHOD, 2},
		 {"Leaf", SymbolKind::CLASS, 3},
		 {"Leaf::visit", SymbolKind::METHOD, 4}},
		{{ReferenceKind::INHERITANCE, "Leaf", "Node", 3},
		 {ReferenceKind::OVERRIDE, "Leaf::visit", "Node::visit", 4}});
	const auto render = [](const bool dispatches) {
		return recordTranslationUnit(
			"a.cc",
			{{"render", SymbolKind::FUNCTION, 1}},
			{{ReferenceKind::CALL, "render", "Node::visit", 2, true, dispatches}});
	};

	TestStorage full;
	full.inject(header.get());
	full.inject(render(false).get());
	full.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);
	REQUIRE(full.deriveDispatchEdges());
	REQUIRE(dispatchEdges(full).empty());

	TestStorage refreshed;
	refreshed.inject(header.get());
	refreshed.inject(render(true).get());
	refreshed.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);
	REQUIRE(refreshed.deriveDispatchEdges());
	REQUIRE(dispatchEdges(refreshed) == std::set<std::string> {dispatchEdge("render", "Leaf::visit")});

	refreshed.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_CLEAR);
	refreshed.clearFileElements({FilePath("a.cc")}, [](int) {});
	REQUIRE(countCallDispatchComponents(refreshed) == 0);

	refreshed.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_WRITE);
	refreshed.inject(render(false).get());
	refreshed.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);
	REQUIRE(refreshed.deriveDispatchEdges());

	REQUIRE(dispatchEdges(refreshed).empty());
	REQUIRE(describeIndex(refreshed) == describeIndex(full));
	REQUIRE(countCallDispatchComponents(refreshed) == countCallDispatchComponents(full));
}

TEST_CASE("storage refresh keeps a satisfies edge with the file that proves it")
{
	// shape.h declares the concept, circle.h the type, render.cc proves Circle satisfies Shape
	// (render(circle) with render constrained by Shape): the edge is located there, in a file
	// that is neither the type's nor the concept's.
	const std::shared_ptr<IntermediateStorage> shape = recordTranslationUnit(
		"shape.h", {{"Shape", SymbolKind::CONCEPT, 1}}, {});
	const std::shared_ptr<IntermediateStorage> circle = recordTranslationUnit(
		"circle.h", {{"Circle", SymbolKind::STRUCT, 1}}, {});
	const auto render = [](const bool proves) {
		std::vector<RecordedReference> references {{ReferenceKind::CALL, "use", "render", 3}};
		if (proves)
		{
			references.push_back({ReferenceKind::SATISFIES, "Circle", "Shape", 3});
		}
		return recordTranslationUnit(
			"render.cc", {{"use", SymbolKind::FUNCTION, 2}, {"render", SymbolKind::FUNCTION, 1}}, references);
	};

	TestStorage full;
	full.inject(shape.get());
	full.inject(circle.get());
	full.inject(render(true).get());
	REQUIRE(hasEdge(full, Edge::EDGE_SATISFIES, "Circle", "Shape"));

	SECTION("the proving file is reindexed: the edge goes with it and comes back")
	{
		TestStorage refreshed;
		refreshed.inject(shape.get());
		refreshed.inject(circle.get());
		refreshed.inject(render(true).get());
		refreshed.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_CLEAR);
		refreshed.clearFileElements({FilePath("render.cc")}, [](int) {});
		REQUIRE(!hasEdge(refreshed, Edge::EDGE_SATISFIES, "Circle", "Shape"));
		REQUIRE(refreshed.getNodeIdForNameHierarchy(createNameHierarchy("Circle")) != 0);

		refreshed.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_WRITE);
		refreshed.inject(render(true).get());
		REQUIRE(describeIndex(refreshed) == describeIndex(full));
	}

	SECTION("the proof is gone from the reindexed file: so is the edge")
	{
		TestStorage noProof;
		noProof.inject(shape.get());
		noProof.inject(circle.get());
		noProof.inject(render(false).get());

		TestStorage refreshed;
		refreshed.inject(shape.get());
		refreshed.inject(circle.get());
		refreshed.inject(render(true).get());
		refreshed.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_CLEAR);
		refreshed.clearFileElements({FilePath("render.cc")}, [](int) {});
		refreshed.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_WRITE);
		refreshed.inject(render(false).get());
		REQUIRE(describeIndex(refreshed) == describeIndex(noProof));
	}

	SECTION("the type's header changes: a refresh reindexes it and the file including it")
	{
		TestStorage refreshed;
		refreshed.inject(shape.get());
		refreshed.inject(circle.get());
		refreshed.inject(render(true).get());
		refreshed.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_CLEAR);
		refreshed.clearFileElements({FilePath("circle.h"), FilePath("render.cc")}, [](int) {});
		REQUIRE(!hasEdge(refreshed, Edge::EDGE_SATISFIES, "Circle", "Shape"));
		refreshed.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_WRITE);
		refreshed.inject(circle.get());
		refreshed.inject(render(true).get());
		REQUIRE(describeIndex(refreshed) == describeIndex(full));
	}

	SECTION("as many proofs as files, one edge; merging shards is the same inject")
	{
		const std::shared_ptr<IntermediateStorage> other = recordTranslationUnit(
			"other.cc",
			{{"other", SymbolKind::FUNCTION, 1}},
			{{ReferenceKind::SATISFIES, "Circle", "Shape", 2}});
		TestStorage shardA;
		shardA.inject(shape.get());
		shardA.inject(circle.get());
		shardA.inject(render(true).get());
		TestStorage shardB;
		shardB.inject(shape.get());
		shardB.inject(circle.get());
		shardB.inject(other.get());

		TestStorage merged;
		merged.inject(&shardA);
		merged.inject(&shardB);
		std::size_t satisfiesEdges = 0;
		for (const StorageEdge& edge: merged.getStorageEdges())
		{
			satisfiesEdges += edge.type == Edge::EDGE_SATISFIES ? 1 : 0;
		}
		REQUIRE(satisfiesEdges == 1);
		REQUIRE(hasEdge(merged, Edge::EDGE_SATISFIES, "Circle", "Shape"));
	}
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

// The edges of `storage` without occurrences of their own, member edges left out.
size_t countEdgesWithoutOccurrences(const IntermediateStorage& storage)
{
	std::set<Id> elementsWithOccurrences;
	for (const StorageOccurrence& occurrence: storage.getStorageOccurrences())
	{
		elementsWithOccurrences.insert(occurrence.elementId);
	}
	return static_cast<size_t>(std::ranges::count_if(storage.getStorageEdges(), [&](const StorageEdge& edge) {
		return edge.type != Edge::EDGE_MEMBER && !elementsWithOccurrences.contains(edge.id);
	}));
}
}	 // namespace

TEST_CASE("storage refresh of one parsed file leaves the index a full index would have")
{
	// leveldb's db/filename.cc forward-declares WriteStringToFileSync, which util/env.cc defines;
	// clearing filename.cc deleted the call in env.cc and the namespace's member edges to all
	// that env.cc declares. Before, filename.cc instantiated a template with a lambda (repro's
	// lib::overloaded), whose return type the indexer records without a location.
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
		"template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };\n"
		"template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;\n"
		"int SetCurrentFile(Env* env) {\n"
		"  auto ov = overloaded{[](char) { return 1; }};\n"
		"  return ov('c') + WriteStringToFileSync(env);\n"
		"}\n"
		"}\n");
	const std::shared_ptr<IntermediateStorage> filenameAfter = parseFile(
		"filename.cc",
		"namespace leveldb {\n"
		"struct Env;\n"
		"int WriteStringToFileSync(Env* env);\n"
		"struct Added {};\n"
		"int SetCurrentFile(Env* env) { return WriteStringToFileSync(env); }\n"
		"}\n");

	// The lambda's return type is a use in implicit code (the instantiation), without a location.
	REQUIRE(countEdgesWithoutOccurrences(*filenameBefore) > countEdgesWithoutOccurrences(*filenameAfter));

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

#if BUILD_CXX_LANGUAGE_PACKAGE
TEST_CASE("storage derives dispatch edges from what the C++ indexer records")
{
	const std::shared_ptr<IntermediateStorage> shapes = parseFile(
		"shapes.cc",
		"struct Shape { virtual ~Shape() {} virtual void draw() = 0; };\n"
		"struct Circle : Shape { void draw() override {} };\n"
		"struct Wheel : Circle {\n"
		"  ~Wheel() override {}\n"
		"  void draw() override { Circle::draw(); }\n"
		"};\n"
		"struct Square : Shape { void draw() override {} };\n"
		"void render(Shape* shape) { shape->draw(); }\n"
		"void destroy(Shape* shape) { delete shape; }\n");

	TestStorage storage;
	storage.inject(shapes.get());
	storage.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);
	REQUIRE(storage.deriveDispatchEdges());

	// The call of the pure virtual Shape::draw reaches every override of it, and delete every
	// destructor, the implicit ones included. Wheel's Circle::draw() and the implicit call of
	// Circle's destructor from Wheel's are calls of the base implementation: they derive nothing.
	REQUIRE(
		dispatchEdges(storage) ==
		std::set<std::string> {
			dispatchEdge("render", "Circle::draw"),
			dispatchEdge("render", "Wheel::draw"),
			dispatchEdge("render", "Square::draw"),
			dispatchEdge("destroy", "Circle::~Circle"),
			dispatchEdge("destroy", "Wheel::~Wheel"),
			dispatchEdge("destroy", "Square::~Square")});
	REQUIRE(dispatchEdgeLines(storage, "render", "Wheel::draw") == std::set<size_t> {8});
}
#endif

#if BUILD_CXX_LANGUAGE_PACKAGE
namespace
{
// What the C++ indexer said of the calls from `caller` to `callee` in `storage`: true if they
// dispatch, false if they are bound at compile time, nothing if it said nothing.
std::optional<bool> callDispatch(
	const IntermediateStorage& storage, const std::string& caller, const std::string& callee)
{
	std::map<Id, std::string> names;
	for (const StorageNode& node: storage.getStorageNodes())
	{
		names.emplace(node.id, NameHierarchy::deserialize(node.serializedName).getQualifiedName());
	}
	std::optional<bool> result;
	for (const StorageEdge& edge: storage.getStorageEdges())
	{
		if (edge.type != Edge::EDGE_CALL || names[edge.sourceNodeId] != caller ||
			names[edge.targetNodeId] != callee)
		{
			continue;
		}
		for (const StorageElementComponent& component: storage.getElementComponents())
		{
			if (component.elementId != edge.id)
			{
				continue;
			}
			if (component.type == ElementComponentKind::ELEMENT_COMPONENT_CALL_DISPATCHES)
			{
				result = true;
			}
			else if (component.type == ElementComponentKind::ELEMENT_COMPONENT_CALL_STATIC && !result)
			{
				result = false;
			}
		}
	}
	return result;
}

bool hasCall(const IntermediateStorage& storage, const std::string& caller, const std::string& callee)
{
	std::map<Id, std::string> names;
	for (const StorageNode& node: storage.getStorageNodes())
	{
		names.emplace(node.id, NameHierarchy::deserialize(node.serializedName).getQualifiedName());
	}
	return std::ranges::any_of(storage.getStorageEdges(), [&](const StorageEdge& edge) {
		return edge.type == Edge::EDGE_CALL && names[edge.sourceNodeId] == caller &&
			names[edge.targetNodeId] == callee;
	});
}
}	 // namespace

TEST_CASE("cxx indexer records whether a call dispatches, as clang decides it")
{
	const std::shared_ptr<IntermediateStorage> storage = parseFile(
		"calls.cc",
		"struct Base { virtual ~Base() {} virtual void f() {} };\n"
		"struct Derived : Base {\n"
		"  Derived() { f(); }\n"
		"  ~Derived() override { f(); }\n"
		"  void f() override { Base::f(); }\n"
		"  void g() { f(); }\n"
		"  int n = (f(), 0);\n"
		"};\n"
		"struct Sealed final : Base { void f() override {} };\n"
		"struct Plain { ~Plain() {} };\n"
		"struct Node { virtual ~Node() {} virtual void traverse() {} };\n"
		"struct Group : Node { Node* child; void traverse() override { child->traverse(); } };\n"
		"struct Eq { virtual bool operator==(const Eq&) const { return true; } };\n"
		"void onPointer(Base* b) { b->f(); }\n"
		"void onReference(Base& b) { b.f(); }\n"
		"void onObject() { Derived d; d.f(); }\n"
		"void onMember(Group& g) { g.child->traverse(); }\n"
		"void onSealed(Sealed* s) { s->f(); }\n"
		"void qualified(Derived* d) { d->Base::f(); }\n"
		"void deleteVirtual(Base* b) { delete b; }\n"
		"void deletePlain(Plain* p) { delete p; }\n"
		"void plainObject() { Plain p; }\n"
		"bool compare(const Eq& a, const Eq& b) { return a == b; }\n"
		"bool compareObjects() { Eq a; Eq b; return a == b; }\n"
		"template <class T> void generic(T* t) { t->f(); }\n"
		"void instantiate(Base* b) { generic(b); }\n");

	// through a pointer or a reference: dispatches, from a method on `this` too
	REQUIRE(callDispatch(*storage, "onPointer", "Base::f") == true);
	REQUIRE(callDispatch(*storage, "onReference", "Base::f") == true);
	REQUIRE(callDispatch(*storage, "Derived::g", "Derived::f") == true);
	REQUIRE(callDispatch(*storage, "onMember", "Node::traverse") == true);
	// the forwarding override: a call from Group::traverse to the method it overrides that does
	// dispatch, which the derivation's guess got wrong
	REQUIRE(callDispatch(*storage, "Group::traverse", "Node::traverse") == true);

	// qualified: Base::f() in the override, d->Base::f() outside
	REQUIRE(callDispatch(*storage, "Derived::f", "Base::f") == false);
	REQUIRE(callDispatch(*storage, "qualified", "Base::f") == false);
	// on an object, not through a pointer or a reference
	REQUIRE(callDispatch(*storage, "onObject", "Derived::f") == false);
	// a final class has no overrides to reach
	REQUIRE(callDispatch(*storage, "onSealed", "Sealed::f") == false);
	// on `this` while the object is built or torn down, member initializers included
	REQUIRE(callDispatch(*storage, "Derived::Derived", "Derived::f") == false);
	REQUIRE(callDispatch(*storage, "Derived::~Derived", "Derived::f") == false);
	REQUIRE(callDispatch(*storage, "Derived::n", "Derived::f") == false);

	// destructors: the implicit ones never dispatch -- the base's from the derived one, the
	// local object's at the end of its scope -- delete does when the destructor is virtual
	REQUIRE(callDispatch(*storage, "Derived::~Derived", "Base::~Base") == false);
	REQUIRE(callDispatch(*storage, "onObject", "Derived::~Derived") == false);
	REQUIRE(callDispatch(*storage, "deleteVirtual", "Base::~Base") == true);
	// a destructor that is not virtual has no overrides: nothing to say, and nothing said
	REQUIRE(hasCall(*storage, "deletePlain", "Plain::~Plain"));
	REQUIRE(callDispatch(*storage, "deletePlain", "Plain::~Plain") == std::nullopt);
	REQUIRE(hasCall(*storage, "plainObject", "Plain::~Plain"));
	REQUIRE(callDispatch(*storage, "plainObject", "Plain::~Plain") == std::nullopt);

	// a member operator, its object the first operand
	REQUIRE(callDispatch(*storage, "compare", "Eq::operator==") == true);
	REQUIRE(callDispatch(*storage, "compareObjects", "Eq::operator==") == false);
}

TEST_CASE("storage derives dispatch edges only from the C++ calls that dispatch")
{
	const std::shared_ptr<IntermediateStorage> nodes = parseFile(
		"nodes.cc",
		"struct Node { virtual ~Node() {} virtual void traverse() {} };\n"
		"struct Leaf : Node { void traverse() override {} };\n"
		"struct Group : Node {\n"
		"  Node* child;\n"
		"  ~Group() override {}\n"
		"  void traverse() override { child->traverse(); Node::traverse(); }\n"
		"};\n"
		"struct Logged : Leaf { void traverse() override { Leaf::traverse(); } };\n"
		"void walk(Node* node) { node->traverse(); }\n"
		"void walkLeaf() { Leaf leaf; leaf.traverse(); }\n");

	TestStorage storage;
	storage.inject(nodes.get());
	storage.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);
	REQUIRE(storage.deriveDispatchEdges());

	// Group::traverse forwards to its child and calls Node::traverse() too: one edge to
	// Node::traverse, which dispatches, so Group's edges come back. Logged's Leaf::traverse() and
	// the destructors' implicit calls of their bases', bound at compile time, derive nothing;
	// neither does a call on a local Leaf.
	REQUIRE(
		dispatchEdges(storage) ==
		std::set<std::string> {
			dispatchEdge("walk", "Leaf::traverse"),
			dispatchEdge("walk", "Group::traverse"),
			dispatchEdge("walk", "Logged::traverse"),
			dispatchEdge("Group::traverse", "Leaf::traverse"),
			dispatchEdge("Group::traverse", "Group::traverse"),
			dispatchEdge("Group::traverse", "Logged::traverse")});
}
#endif


namespace
{
// Nodes by qualified name, without signature: a template's node is "render<Shape T>".
std::map<Id, std::string> qualifiedNames(const PersistentStorage& storage)
{
	std::map<Id, std::string> names;
	for (const StorageNode& node: storage.getStorageNodes())
	{
		names.emplace(node.id, NameHierarchy::deserialize(node.serializedName).getQualifiedName());
	}
	return names;
}

// The static dispatch edges of `storage`, as "template -> callee".
std::set<std::string> staticDispatchEdges(const PersistentStorage& storage)
{
	const std::map<Id, std::string> names = qualifiedNames(storage);
	std::set<std::string> edges;
	for (const StorageEdge& edge: storage.getStorageEdges())
	{
		if (edge.type == Edge::EDGE_STATIC_DISPATCH)
		{
			edges.insert(names.at(edge.sourceNodeId) + " -> " + names.at(edge.targetNodeId));
		}
	}
	return edges;
}

std::optional<StorageEdge> findStaticDispatchEdge(
	const PersistentStorage& storage, const std::string& source, const std::string& target)
{
	const std::map<Id, std::string> names = qualifiedNames(storage);
	for (const StorageEdge& edge: storage.getStorageEdges())
	{
		if (edge.type == Edge::EDGE_STATIC_DISPATCH && names.at(edge.sourceNodeId) == source &&
			names.at(edge.targetNodeId) == target)
		{
			return edge;
		}
	}
	return std::nullopt;
}

// The instantiations a static dispatch edge was derived from, by name.
std::set<std::string> staticDispatchInstantiations(
	const PersistentStorage& storage, const std::string& source, const std::string& target)
{
	const std::optional<StorageEdge> edge = findStaticDispatchEdge(storage, source, target);
	REQUIRE(edge);
	std::map<std::string, std::string> namesById;
	for (const auto& [id, name]: qualifiedNames(storage))
	{
		namesById.emplace(std::to_string(static_cast<Id::type>(id)), name);
	}
	std::set<std::string> result;
	for (const StorageElementComponent& component: storage.getElementComponents())
	{
		if (component.elementId == edge->id &&
			component.type == ElementComponentKind::ELEMENT_COMPONENT_STATIC_DISPATCH_VIA)
		{
			result.insert(namesById.at(component.data));
		}
	}
	return result;
}

// Where a static dispatch edge occurs, as "line:column".
std::set<std::string> staticDispatchLocations(
	const PersistentStorage& storage, const std::string& source, const std::string& target)
{
	const std::optional<StorageEdge> edge = findStaticDispatchEdge(storage, source, target);
	REQUIRE(edge);
	std::map<Id, StorageSourceLocation> locations;
	for (const StorageSourceLocation& location: storage.getStorageSourceLocations())
	{
		locations.emplace(location.id, location);
	}
	std::set<std::string> result;
	for (const StorageOccurrence& occurrence: storage.getStorageOccurrences())
	{
		if (occurrence.elementId == edge->id)
		{
			const StorageSourceLocation& location = locations.at(occurrence.sourceLocationId);
			result.insert(std::format("{}:{}", location.startLine, location.startCol));
		}
	}
	return result;
}

std::size_t countStaticDispatchComponents(const PersistentStorage& storage)
{
	return static_cast<std::size_t>(std::ranges::count_if(
		storage.getElementComponents(), [](const StorageElementComponent& component) {
			return component.type == ElementComponentKind::ELEMENT_COMPONENT_STATIC_DISPATCH_VIA;
		}));
}

// render.h's template, as one translation unit records it; use.cc instantiates it with each of
// `arguments` (render<Circle> calling Circle::draw at render's t.draw(), line 1).
std::shared_ptr<IntermediateStorage> recordRenderUse(const std::vector<std::string>& arguments)
{
	std::vector<RecordedSymbol> symbols {{"use", SymbolKind::FUNCTION, 1}};
	std::vector<RecordedReference> references;
	for (const std::string& argument: arguments)
	{
		symbols.push_back({"render<" + argument + ">", SymbolKind::FUNCTION, 1, DefinitionKind::IMPLICIT});
		references.push_back({ReferenceKind::TEMPLATE_SPECIALIZATION, "render<" + argument + ">", "render", 1});
		references.push_back({ReferenceKind::CALL, "render<" + argument + ">", argument + "::draw", 1});
		references.push_back({ReferenceKind::CALL, "use", "render<" + argument + ">", 2});
	}
	return recordTranslationUnit("use.cc", symbols, references);
}
}	 // namespace

TEST_CASE("storage refresh derives the static dispatch edges a full index would have")
{
	// shape.h declares the types, render.h the template, use.cc instantiates it: with Circle and
	// Square before the change, with Square only after.
	const std::shared_ptr<IntermediateStorage> shape = recordTranslationUnit(
		"shape.h",
		{{"Circle", SymbolKind::STRUCT, 1},
		 {"Circle::draw", SymbolKind::METHOD, 2},
		 {"Square", SymbolKind::STRUCT, 3},
		 {"Square::draw", SymbolKind::METHOD, 4}},
		{});
	const std::shared_ptr<IntermediateStorage> render =
		recordTranslationUnit("render.h", {{"render", SymbolKind::FUNCTION, 1}}, {});

	TestStorage full;
	full.inject(shape.get());
	full.inject(render.get());
	full.inject(recordRenderUse({"Square"}).get());
	full.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);
	REQUIRE(full.deriveStaticDispatchEdges());
	REQUIRE(staticDispatchEdges(full) == std::set<std::string> {"render -> Square::draw"});

	TestStorage refreshed;
	refreshed.inject(shape.get());
	refreshed.inject(render.get());
	refreshed.inject(recordRenderUse({"Circle", "Square"}).get());
	refreshed.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);
	const std::expected<std::size_t, DerivedEdgeError> before = refreshed.deriveStaticDispatchEdges();
	REQUIRE(before);
	REQUIRE(*before == 2);
	REQUIRE(
		staticDispatchEdges(refreshed) ==
		std::set<std::string> {"render -> Circle::draw", "render -> Square::draw"});

	// Deriving again changes nothing: the edges, their ids and their instantiations.
	const std::set<std::string> once = describeIndex(refreshed);
	const std::size_t components = countStaticDispatchComponents(refreshed);
	REQUIRE(refreshed.deriveStaticDispatchEdges());
	REQUIRE(describeIndex(refreshed) == once);
	REQUIRE(countStaticDispatchComponents(refreshed) == components);
	REQUIRE(components == 2);

	// as a refresh does: clear, inject the files indexed again, then derive over everything
	refreshed.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_CLEAR);
	refreshed.clearFileElements({FilePath("use.cc")}, [](int) {});
	REQUIRE(staticDispatchEdges(refreshed).empty());
	REQUIRE(countStaticDispatchComponents(refreshed) == 0);

	refreshed.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_WRITE);
	refreshed.inject(recordRenderUse({"Square"}).get());
	refreshed.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);
	REQUIRE(refreshed.deriveStaticDispatchEdges());

	REQUIRE(describeIndex(refreshed) == describeIndex(full));
	REQUIRE(
		staticDispatchInstantiations(refreshed, "render", "Square::draw") ==
		std::set<std::string> {"render<Square>"});
}

#if BUILD_CXX_LANGUAGE_PACKAGE
namespace
{
const std::string staticDispatchCode =
	"template <typename T> concept Shape = requires(T& t) { t.draw(); };\n"
	"struct Circle { void draw() {} };\n"
	"struct Square { void draw() {} };\n"
	"void log() {}\n"
	"template <typename T> void helper(T& t) { t.draw(); log(); }\n"
	"template <Shape T> void render(T& t) { t.draw(); helper(t); log(); }\n"
	"template <typename T> void outer(T& t) { helper(t); }\n"
	"template <typename T> struct Painter { T shape; void paint() { shape.draw(); }\n"
	"  template <typename U> void with(U& u) { u.draw(); shape.draw(); } };\n"
	"struct Canvas { template <typename T> void show(T& t) { t.draw(); } };\n"
	"template <typename T> void both(T& t, Circle& c) { t.draw(); c.draw(); }\n"
	"template <typename T> void pong(T& t, int n);\n"
	"template <typename T> void ping(T& t, int n) { if (n) pong(t, n - 1); }\n"
	"template <typename T> void pong(T& t, int n) { t.draw(); if (n) ping(t, n - 1); }\n"
	"template <typename T, typename U> void pair(T& t, U) { t.draw(); }\n"
	"void use() { Circle c; Square s; render(c); render(s); outer(c);\n"
	"  Painter<Circle> p; p.paint(); p.with(s); Canvas k; k.show(c);\n"
	"  both(s, c); both(c, c); ping(c, 2); pair(c, 1); pair(c, 'x'); }\n";
}	 // namespace

TEST_CASE("storage derives static dispatch edges from what the C++ indexer records of instantiations")
{
	TestStorage storage;
	storage.inject(parseFile("render.cc", staticDispatchCode).get());
	storage.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);
	const std::expected<std::size_t, DerivedEdgeError> derived = storage.deriveStaticDispatchEdges();
	REQUIRE(derived);

	const std::set<std::string> expected {
		// a function template's dependent calls, for each argument it is instantiated with
		"helper<typename T> -> Circle::draw",
		"helper<typename T> -> Square::draw",
		"render<Shape T> -> Circle::draw",
		"render<Shape T> -> Square::draw",
		// outer calls draw only through helper<Circle>: the chain is collapsed
		"outer<typename T> -> Circle::draw",
		// a member of a class template, dependent on the class's argument; a member template of
		// it, on both; a member template of a plain class
		"Painter<typename T>::paint -> Circle::draw",
		"Painter<typename T>::with<typename U> -> Square::draw",
		"Painter<typename T>::with<typename U> -> Circle::draw",
		"Canvas::show<typename T> -> Circle::draw",
		// both calls Circle::draw itself (c.draw()): only its argument's draw is derived
		"both<typename T> -> Square::draw",
		// mutually recursive templates: a cycle, which ends
		"ping<typename T> -> Circle::draw",
		"pong<typename T> -> Circle::draw",
		"pair<typename T, typename U> -> Circle::draw",
		// nothing to log (every template calls it itself), nothing to helper<Circle> or any other
		// instantiation
	};
	REQUIRE(staticDispatchEdges(storage) == expected);
	REQUIRE(*derived == expected.size());

	// Which instantiation: kept with the edge.
	REQUIRE(
		staticDispatchInstantiations(storage, "render<Shape T>", "Circle::draw") ==
		std::set<std::string> {"render<Circle>"});
	REQUIRE(
		staticDispatchInstantiations(storage, "pair<typename T, typename U>", "Circle::draw") ==
		std::set<std::string> {"pair<Circle, int>", "pair<Circle, char>"});
	REQUIRE(
		staticDispatchInstantiations(storage, "Painter<typename T>::paint", "Circle::draw") ==
		std::set<std::string> {"Painter<Circle>::paint"});

	// Located in the template's own body: at the dependent call, and at the call of the helper
	// template that leads on to the callee.
	REQUIRE(staticDispatchLocations(storage, "helper<typename T>", "Circle::draw") == std::set<std::string> {"5:45"});
	REQUIRE(
		staticDispatchLocations(storage, "render<Shape T>", "Circle::draw") ==
		std::set<std::string> {"6:42", "6:50"});
	REQUIRE(staticDispatchLocations(storage, "outer<typename T>", "Circle::draw") == std::set<std::string> {"7:42"});

	// The calls themselves are as they were: the template calls only what does not depend on T.
	REQUIRE(!hasEdge(storage, Edge::EDGE_CALL, "render", "Circle::draw"));
}

TEST_CASE("storage derives the same static dispatch edges when it derives them again")
{
	TestStorage storage;
	storage.inject(parseFile("render.cc", staticDispatchCode).get());
	storage.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);
	REQUIRE(storage.deriveDispatchEdges());
	REQUIRE(storage.deriveStaticDispatchEdges());
	const std::set<std::string> once = describeIndex(storage);
	const std::size_t components = countStaticDispatchComponents(storage);

	REQUIRE(storage.deriveStaticDispatchEdges());
	REQUIRE(storage.deriveDispatchEdges());
	REQUIRE(describeIndex(storage) == once);
	REQUIRE(countStaticDispatchComponents(storage) == components);

	// Clearing the file takes everything, the derived edges and their components included.
	storage.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_CLEAR);
	storage.clearFileElements({FilePath("render.cc")}, [](int) {});
	REQUIRE(staticDispatchEdges(storage).empty());
	REQUIRE(countStaticDispatchComponents(storage) == 0);
	REQUIRE(storage.getStorageNodes().empty());
}

TEST_CASE("graph of a template names the instantiations of its static dispatch edges")
{
	TestStorage storage;
	storage.inject(parseFile("render.cc", staticDispatchCode).get());
	storage.setMode(SqliteIndexStorage::StorageModeType::STORAGE_MODE_READ);
	REQUIRE(storage.deriveStaticDispatchEdges());
	storage.buildCaches();

	Id pairId;
	for (const auto& [id, name]: qualifiedNames(storage))
	{
		if (name == "pair<typename T, typename U>")
		{
			pairId = id;
		}
	}
	REQUIRE(pairId);

	const std::shared_ptr<Graph> graph = storage.getGraphForActiveTokenIds({pairId}, {});
	std::vector<std::string> instantiations;
	graph->forEachEdge([&instantiations](Edge* edge) {
		if (edge->getType() == Edge::EDGE_STATIC_DISPATCH)
		{
			if (const TokenComponentInstantiations* component =
					edge->getComponent<TokenComponentInstantiations>())
			{
				instantiations = component->names;
			}
		}
	});
	REQUIRE(instantiations == std::vector<std::string> {"pair<Circle, char>", "pair<Circle, int>"});
}
#endif
