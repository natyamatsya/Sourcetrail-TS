#include "Catch2.hpp"

#include "FileSystem.h"
#include "SqliteIndexStorage.h"
#include "StorageConnection.h"
#include "StorageNodeAttribute.h"

TEST_CASE("storage adds node successfully")
{
	FilePath databasePath("data/SQLiteTestSuite/test.sqlite");
	int nodeCount = -1;
	{
		StorageConnection connection(databasePath);
		SqliteIndexStorage storage(connection);
		storage.setup();
		storage.beginTransaction();
		storage.addNode(StorageNodeData(NODE_UNDEFINED, "a"));
		storage.commitTransaction();
		nodeCount = storage.getNodeCount();
	}
	FileSystem::remove(databasePath);

	REQUIRE(1 == nodeCount);
}

TEST_CASE("storage round-trips node modifiers")
{
	FilePath databasePath("data/SQLiteTestSuite/test.sqlite");
	NodeModifierMask actorModifiers = NODE_MODIFIER_NONE;
	NodeModifierMask plainModifiers = NODE_MODIFIER_ACTOR;  // sentinel; expect it reset to NONE
	{
		StorageConnection connection(databasePath);
		SqliteIndexStorage storage(connection);
		storage.setup();
		storage.beginTransaction();
		const Id actorId = storage.addNode(
			StorageNodeData(NODE_CLASS, "MyActor", NODE_MODIFIER_ACTOR));
		const Id plainId = storage.addNode(StorageNodeData(NODE_CLASS, "MyClass"));
		storage.commitTransaction();
		actorModifiers = storage.getNodeById(actorId).modifiers;
		plainModifiers = storage.getNodeById(plainId).modifiers;
	}
	FileSystem::remove(databasePath);

	REQUIRE(NODE_MODIFIER_ACTOR == actorModifiers);
	REQUIRE(NODE_MODIFIER_NONE == plainModifiers);
}

TEST_CASE("storage round-trips node attributes")
{
	FilePath databasePath("data/SQLiteTestSuite/test.sqlite");
	std::vector<StorageNodeAttribute> attributes;
	{
		StorageConnection connection(databasePath);
		SqliteIndexStorage storage(connection);
		storage.setup();
		storage.beginTransaction();
		const Id nodeId = storage.addNode(StorageNodeData(NODE_CLASS, "Model"));
		storage.addNodeAttribute(
			StorageNodeAttribute(nodeId, NodeAttributeKind::AVAILABILITY, "@available(macOS 14, *)"));
		storage.addNodeAttribute(
			StorageNodeAttribute(nodeId, NodeAttributeKind::DEPRECATED, "use NewModel"));
		// A repeated (node, key, value) triple is deduped by the primary key.
		storage.addNodeAttribute(
			StorageNodeAttribute(nodeId, NodeAttributeKind::AVAILABILITY, "@available(macOS 14, *)"));
		storage.commitTransaction();
		attributes = storage.getNodeAttributesByNodeIds({nodeId});
	}
	FileSystem::remove(databasePath);

	REQUIRE(2 == attributes.size());
	bool sawAvailability = false;
	bool sawDeprecated = false;
	for (const StorageNodeAttribute& attribute: attributes)
	{
		if (attribute.key == NodeAttributeKind::AVAILABILITY)
		{
			sawAvailability = true;
			REQUIRE(attribute.value == "@available(macOS 14, *)");
		}
		else if (attribute.key == NodeAttributeKind::DEPRECATED)
		{
			sawDeprecated = true;
			REQUIRE(attribute.value == "use NewModel");
		}
	}
	REQUIRE(sawAvailability);
	REQUIRE(sawDeprecated);
}

TEST_CASE("storage removes node successfully")
{
	FilePath databasePath("data/SQLiteTestSuite/test.sqlite");
	int nodeCount = -1;
	{
		StorageConnection connection(databasePath);
		SqliteIndexStorage storage(connection);
		storage.setup();
		storage.beginTransaction();
		Id nodeId = storage.addNode(StorageNodeData(NODE_UNDEFINED, "a"));
		storage.removeElement(nodeId);
		storage.commitTransaction();
		nodeCount = storage.getNodeCount();
	}
	FileSystem::remove(databasePath);

	REQUIRE(0 == nodeCount);
}

TEST_CASE("storage adds edge successfully")
{
	FilePath databasePath("data/SQLiteTestSuite/test.sqlite");
	int edgeCount = -1;
	{
		StorageConnection connection(databasePath);
		SqliteIndexStorage storage(connection);
		storage.setup();
		storage.beginTransaction();
		Id sourceNodeId = storage.addNode(StorageNodeData(NODE_UNDEFINED, "a"));
		Id targetNodeId = storage.addNode(StorageNodeData(NODE_UNDEFINED, "b"));
		storage.addEdge(StorageEdgeData(Edge::EDGE_UNDEFINED, sourceNodeId, targetNodeId));
		storage.commitTransaction();
		edgeCount = storage.getEdgeCount();
	}
	FileSystem::remove(databasePath);

	REQUIRE(1 == edgeCount);
}

TEST_CASE("storage removes edge successfully")
{
	FilePath databasePath("data/SQLiteTestSuite/test.sqlite");
	int edgeCount = -1;
	{
		StorageConnection connection(databasePath);
		SqliteIndexStorage storage(connection);
		storage.setup();
		storage.beginTransaction();
		Id sourceNodeId = storage.addNode(StorageNodeData(NODE_UNDEFINED, "a"));
		Id targetNodeId = storage.addNode(StorageNodeData(NODE_UNDEFINED, "b"));
		Id edgeId = storage.addEdge(StorageEdgeData(Edge::EDGE_UNDEFINED, sourceNodeId, targetNodeId));
		storage.removeElement(edgeId);
		storage.commitTransaction();
		edgeCount = storage.getEdgeCount();
	}
	FileSystem::remove(databasePath);

	REQUIRE(0 == edgeCount);
}
