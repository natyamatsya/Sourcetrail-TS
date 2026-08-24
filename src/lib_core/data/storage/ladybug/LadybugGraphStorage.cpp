// Compiled only for the optional Kùzu (LadybugDB) backend; otherwise an empty TU
// (the lib globs its sources). Pulls in the Kùzu adapter header only here.
#ifdef SOURCETRAIL_USE_LADYBUG

#include "LadybugGraphStorage.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <utility>

#ifndef SRCTRL_MODULE_BUILD
#include "FilePath.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.file;
#endif

namespace
{
// The property-graph schema mirroring Sourcetrail's node/edge model. `id` is the
// shared element id (SQLite primary key), reused as the Kùzu primary key.
constexpr const char* kNodeTable =
	"CREATE NODE TABLE IF NOT EXISTS Node("
	"id INT64, type INT64, serializedName STRING, PRIMARY KEY(id));";

// One relationship table per edge type rather than a single table discriminated by a
// `type` property. Kùzu matches relationship patterns by table, so a typed traversal
// against one shared table has to read every edge and filter; measured over this
// project's own index, a two-hop pattern took 154 ms against a shared table and 29 ms
// against typed ones (context/EXPERIMENT_LADYBUG_MIRROR.md).
//
// The names are a storage contract, so they are spelled out here rather than derived
// from Edge::getReadableTypeString(), which exists to be shown to a human and is free
// to change for display reasons.
struct EdgeTable
{
	Edge::EdgeType type;
	const char* name;
};

constexpr EdgeTable kEdgeTables[] = {
	{Edge::EDGE_MEMBER, "Member"},
	{Edge::EDGE_TYPE_USAGE, "TypeUsage"},
	{Edge::EDGE_USAGE, "Usage"},
	{Edge::EDGE_CALL, "Call"},
	{Edge::EDGE_INHERITANCE, "Inheritance"},
	{Edge::EDGE_OVERRIDE, "Override"},
	{Edge::EDGE_TYPE_ARGUMENT, "TypeArgument"},
	{Edge::EDGE_TEMPLATE_SPECIALIZATION, "TemplateSpecialization"},
	{Edge::EDGE_INCLUDE, "Include"},
	{Edge::EDGE_IMPORT, "Import"},
	{Edge::EDGE_BUNDLED_EDGES, "BundledEdges"},
	{Edge::EDGE_MACRO_USAGE, "MacroUsage"},
	{Edge::EDGE_ANNOTATION_USAGE, "AnnotationUsage"},
	{Edge::EDGE_BINDS, "Binds"},
};

// Anything the table above does not name -- a type added to Edge::EdgeType without a
// matching entry here -- still gets mirrored, into a shared table carrying the raw
// type. Losing an edge silently would be worse than losing the traversal speed.
constexpr const char* kFallbackEdgeTable = "Edge";

// Kùzu reads standard CSV: quote every field and double any embedded quote, which
// covers the tabs, commas and newlines that Sourcetrail's serialized names contain.
std::string csvQuote(const std::string& value)
{
	std::string quoted;
	quoted.reserve(value.size() + 2);
	quoted += '"';
	for (const char character: value)
	{
		if (character == '"')
		{
			quoted += '"';
		}
		quoted += character;
	}
	quoted += '"';
	return quoted;
}

const char* edgeTableFor(Edge::EdgeType type) noexcept
{
	for (const EdgeTable& table: kEdgeTables)
	{
		if (table.type == type)
		{
			return table.name;
		}
	}
	return kFallbackEdgeTable;
}
}  // namespace

LadybugGraphStorage::LadybugGraphStorage(
	std::unique_ptr<ladybug::LadybugConnection> connection, std::string stagingPrefix)
	: m_connection(std::move(connection)), m_stagingPrefix(std::move(stagingPrefix))
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

	auto storage = std::unique_ptr<LadybugGraphStorage>(
		new LadybugGraphStorage(std::move(*connection), databaseDir.str() + ".staging"));
	if (auto schema = storage->setupSchema(); !schema)
	{
		return std::unexpected(schema.error());
	}
	return storage;
}

stdext::expected<void, std::string> LadybugGraphStorage::setupSchema() noexcept
{
	if (auto result = m_connection->execute(kNodeTable); !result)
	{
		return result;
	}

	for (const EdgeTable& table: kEdgeTables)
	{
		const std::string ddl = std::string{"CREATE REL TABLE IF NOT EXISTS "} + table.name +
			"(FROM Node TO Node, id INT64);";
		if (auto result = m_connection->execute(ddl); !result)
		{
			return result;
		}
	}

	return m_connection->execute(
		std::string{"CREATE REL TABLE IF NOT EXISTS "} + kFallbackEdgeTable +
		"(FROM Node TO Node, id INT64, type INT64);");
}

stdext::expected<void, std::string> LadybugGraphStorage::beginTransaction() noexcept
{
	return m_connection->execute("BEGIN TRANSACTION;");
}

stdext::expected<void, std::string> LadybugGraphStorage::commitTransaction() noexcept
{
	// Flush inside the transaction, so a failed COPY takes the batch down with it
	// rather than leaving a partially mirrored commit behind.
	if (auto flushed = flushStagedRows(); !flushed)
	{
		return flushed;
	}
	return m_connection->execute("COMMIT;");
}

void LadybugGraphStorage::stageRow(const std::string& table, std::string row) noexcept
{
	try
	{
		std::string& staged = m_stagedRows[table];
		staged += row;
		staged += '\n';
	}
	catch (const std::exception&)
	{
		// Staging is best-effort like the rest of the mirror; a failure here surfaces
		// as a short COPY rather than a crash in the indexing path.
	}
}

stdext::expected<void, std::string> LadybugGraphStorage::flushStagedRows() noexcept
{
	// std::map orders by key, and "Node" sorts before every relationship table used
	// here, so endpoints are always in place before the relationships that need them.
	for (auto& [table, rows]: m_stagedRows)
	{
		if (rows.empty())
		{
			continue;
		}

		const std::string path = m_stagingPrefix + "." + table + ".csv";
		try
		{
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			if (!file)
			{
				return std::unexpected("could not open the staging file " + path);
			}
			file << rows;
			if (!file)
			{
				return std::unexpected("could not write the staging file " + path);
			}
		}
		catch (const std::exception& e)
		{
			return std::unexpected(std::string{"staging "} + table + " failed: " + e.what());
		}

		const auto copied = m_connection->execute(
			"COPY " + table + " FROM \'" + path + "\';");
		std::remove(path.c_str());
		if (!copied)
		{
			return copied;
		}
		rows.clear();
	}
	return {};
}

void LadybugGraphStorage::discardStagedRows() noexcept
{
	m_stagedRows.clear();
	m_stagedIds.clear();
}

stdext::expected<void, std::string> LadybugGraphStorage::rollbackTransaction() noexcept
{
	discardStagedRows();
	return m_connection->execute("ROLLBACK;");
}

stdext::expected<void, std::string> LadybugGraphStorage::addNode(
	Id id, const StorageNodeData& data) noexcept
{
	try
	{
		if (!m_stagedIds.insert(id).second)
		{
			// Already staged in this run; the id identifies the element, so the row
			// would be identical and COPY would reject the duplicate key.
			return {};
		}
		stageRow(
			"Node",
			std::to_string(static_cast<std::int64_t>(id)) + "," +
				std::to_string(static_cast<std::int64_t>(data.type)) + "," +
				csvQuote(data.serializedName));
		return {};
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
		if (!m_stagedIds.insert(id).second)
		{
			return {};
		}

		// A relationship COPY takes FROM and TO first, then the properties.
		const std::string endpoints = std::to_string(static_cast<std::int64_t>(data.sourceNodeId)) +
			"," + std::to_string(static_cast<std::int64_t>(data.targetNodeId)) + "," +
			std::to_string(static_cast<std::int64_t>(id));

		const char* const table = edgeTableFor(data.type);
		stageRow(
			table,
			table == kFallbackEdgeTable
				? endpoints + "," + std::to_string(static_cast<std::int64_t>(data.type))
				: endpoints);
		return {};
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
