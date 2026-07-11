#ifndef LADYBUG_GRAPH_STORAGE_H
#define LADYBUG_GRAPH_STORAGE_H

#include <memory>
#include <string>
#include <vector>

#include <stdext/expected>

#include "LadybugConnection.h"
#include "StorageEdge.h"
#include "StorageNode.h"

class FilePath;

// Optional graph-database backend (Kùzu / LadybugDB), enabled by
// -DSOURCETRAIL_USE_LADYBUG=ON. It mirrors Sourcetrail's node/edge graph into a
// native property graph so deep traversals and Cypher queries can run against
// it. It is *additive*: SQLite remains the source of truth (Kùzu is
// single-writer by default — a read/traversal accelerator, not a concurrent
// write store), and mirroring is best-effort.
//
// This deliberately maps the *typed* storage API (addNode/addEdge/...) to
// Cypher rather than reusing the SQL StorageDb/StorageStmt seam, because Kùzu
// speaks Cypher, not SQL. Every method is noexcept and reports failures through
// the expected value channel (ADR-0001); the noexcept is load-bearing for
// stdexec.
class LadybugGraphStorage
{
public:
	// Opens (creating if absent) the graph database directory and installs the
	// schema. Returns nullptr-carrying error on failure.
	static stdext::expected<std::unique_ptr<LadybugGraphStorage>, std::string> open(
		const FilePath& databaseDir) noexcept;

	// Transaction control, mapped onto Kùzu's manual transactions.
	stdext::expected<void, std::string> beginTransaction() noexcept;
	stdext::expected<void, std::string> commitTransaction() noexcept;
	stdext::expected<void, std::string> rollbackTransaction() noexcept;

	// Graph writes. `id` is the SQLite-assigned element id, reused as the Kùzu
	// primary key so the two stores share identity.
	stdext::expected<void, std::string> addNode(Id id, const StorageNodeData& data) noexcept;
	stdext::expected<void, std::string> addEdge(Id id, const StorageEdgeData& data) noexcept;

	// Batch mirrors used by the injection path. Each carries its own id.
	stdext::expected<void, std::string> addNodes(const std::vector<StorageNode>& nodes) noexcept;
	stdext::expected<void, std::string> addEdges(const std::vector<StorageEdge>& edges) noexcept;

	// NOTE (scaffold): source locations, occurrences, local symbols, component
	// accesses and errors are not mirrored yet — the schema + writers for those
	// are the natural next step once the node/edge core is validated end to end.

private:
	explicit LadybugGraphStorage(std::unique_ptr<ladybug::LadybugConnection> connection);
	stdext::expected<void, std::string> setupSchema() noexcept;

	std::unique_ptr<ladybug::LadybugConnection> m_connection;
};

#endif	// LADYBUG_GRAPH_STORAGE_H
