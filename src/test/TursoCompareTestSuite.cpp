// Integration guard for the Turso comparison backend. Only compiled when
// SOURCETRAIL_TURSO_COMPARE is on; otherwise this is an empty translation unit.
//
// It drives real SqliteIndexStorage operations through the Dual* backend (which
// runs SQLite and Turso in lockstep) and asserts that the run produced zero
// divergences. Because the storage layer selects the backend via the
// StorageDb/StorageStmt/StorageQuery aliases, no storage code is aware of Turso —
// exercising the ordinary write path is enough to compare the two engines.
#ifdef SOURCETRAIL_TURSO_COMPARE

#include "Catch2.hpp"

#include "DualSqliteDatabase.h"
#include "FileSystem.h"
#include "SqliteIndexStorage.h"

namespace
{
void removeBoth(const FilePath& dbPath)
{
	FileSystem::remove(dbPath);
	FileSystem::remove(FilePath(dbPath.str() + ".turso"));
}
}  // namespace

TEST_CASE("turso matches sqlite across a representative storage workload")
{
	FilePath databasePath("data/SQLiteTestSuite/turso_compare.sqlite");
	removeBoth(databasePath);

	turso_compare::resetDivergenceCount();

	int nodeCountAfterAdds = -1;
	int nodeCountAfterRemove = -1;
	{
		SqliteIndexStorage storage(databasePath);
		storage.setup();

		storage.beginTransaction();
		const Id a = storage.addNode(StorageNodeData(NODE_UNDEFINED, "a"));
		storage.addNode(StorageNodeData(NODE_UNDEFINED, "b"));
		storage.addNode(StorageNodeData(NODE_UNDEFINED, "c"));
		storage.commitTransaction();
		nodeCountAfterAdds = storage.getNodeCount();

		storage.beginTransaction();
		storage.removeElement(a);
		storage.commitTransaction();
		nodeCountAfterRemove = storage.getNodeCount();
	}
	removeBoth(databasePath);

	// SQLite (the source of truth) must behave exactly as always...
	REQUIRE(3 == nodeCountAfterAdds);
	REQUIRE(2 == nodeCountAfterRemove);
	// ...and Turso must have agreed on every operation along the way.
	REQUIRE(0 == turso_compare::divergenceCount());
}

#endif  // SOURCETRAIL_TURSO_COMPARE
