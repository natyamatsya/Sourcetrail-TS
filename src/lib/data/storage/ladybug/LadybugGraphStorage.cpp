// Compiled only for the optional Kùzu (LadybugDB) backend; otherwise an empty TU
// (the lib globs its sources). Pulls in the Kùzu adapter header only here.
#ifdef SOURCETRAIL_USE_LADYBUG

#include "LadybugGraphStorage.h"

#include <cstdint>

#include "FilePath.h"

namespace
{
// The property-graph schema mirroring Sourcetrail's node/edge model. `id` is the
// shared element id (SQLite primary key), reused as the Kùzu primary key.
constexpr const char* kSchema[] = {
	"CREATE NODE TABLE IF NOT EXISTS Node("
	"id INT64, type INT64, serializedName STRING, PRIMARY KEY(id));",
	"CREATE REL TABLE IF NOT EXISTS Edge(FROM Node TO Node, id INT64, type INT64);",
};
}  // namespace

LadybugGraphStorage::LadybugGraphStorage(std::unique_ptr<ladybug::LadybugConnection> connection)
	: m_connection(std::move(connection))
{
}

stdext::expected<std::unique_ptr<LadybugGraphStorage>, std::string> LadybugGraphStorage::open(
	const FilePath& databaseDir) noexcept
{
	auto connection = ladybug::LadybugConnection::open(databaseDir.str());
	if (!connection)
	{
		return std::unexpected(connection.error());
	}

	auto storage =
		std::unique_ptr<LadybugGraphStorage>(new LadybugGraphStorage(std::move(*connection)));
	if (auto schema = storage->setupSchema(); !schema)
	{
		return std::unexpected(schema.error());
	}
	return storage;
}

stdext::expected<void, std::string> LadybugGraphStorage::setupSchema() noexcept
{
	for (const char* ddl: kSchema)
	{
		if (auto result = m_connection->execute(ddl); !result)
		{
			return result;
		}
	}
	return {};
}

stdext::expected<void, std::string> LadybugGraphStorage::beginTransaction() noexcept
{
	return m_connection->execute("BEGIN TRANSACTION;");
}

stdext::expected<void, std::string> LadybugGraphStorage::commitTransaction() noexcept
{
	return m_connection->execute("COMMIT;");
}

stdext::expected<void, std::string> LadybugGraphStorage::rollbackTransaction() noexcept
{
	return m_connection->execute("ROLLBACK;");
}

stdext::expected<void, std::string> LadybugGraphStorage::addNode(
	Id id, const StorageNodeData& data) noexcept
{
	try
	{
		const ladybug::Params params{
			{"id", static_cast<std::int64_t>(id)},
			{"type", static_cast<std::int64_t>(data.type)},
			{"name", data.serializedName},
		};
		return m_connection->execute(
			"MERGE (n:Node {id: $id}) SET n.type = $type, n.serializedName = $name;", params);
	}
	catch (const std::exception& e)
	{
		return std::unexpected(std::string{e.what()});
	}
}

stdext::expected<void, std::string> LadybugGraphStorage::addEdge(
	Id id, const StorageEdgeData& data) noexcept
{
	try
	{
		const ladybug::Params params{
			{"id", static_cast<std::int64_t>(id)},
			{"type", static_cast<std::int64_t>(data.type)},
			{"src", static_cast<std::int64_t>(data.sourceNodeId)},
			{"tgt", static_cast<std::int64_t>(data.targetNodeId)},
		};
		return m_connection->execute(
			"MATCH (a:Node {id: $src}), (b:Node {id: $tgt}) "
			"CREATE (a)-[:Edge {id: $id, type: $type}]->(b);",
			params);
	}
	catch (const std::exception& e)
	{
		return std::unexpected(std::string{e.what()});
	}
}

stdext::expected<void, std::string> LadybugGraphStorage::addNodes(
	const std::vector<StorageNode>& nodes) noexcept
{
	for (const StorageNode& node: nodes)
	{
		if (auto result = addNode(node.id, node); !result)
		{
			return result;
		}
	}
	return {};
}

stdext::expected<void, std::string> LadybugGraphStorage::addEdges(
	const std::vector<StorageEdge>& edges) noexcept
{
	for (const StorageEdge& edge: edges)
	{
		if (auto result = addEdge(edge.id, edge); !result)
		{
			return result;
		}
	}
	return {};
}

#endif	// SOURCETRAIL_USE_LADYBUG
