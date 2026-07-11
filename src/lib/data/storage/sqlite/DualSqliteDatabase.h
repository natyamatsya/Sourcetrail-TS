#ifndef DUAL_SQLITE_DATABASE_H
#define DUAL_SQLITE_DATABASE_H

// Comparison backend: drives real SQLite and Turso in lockstep and reports any
// divergence. Only compiled when SOURCETRAIL_TURSO_COMPARE is defined; the
// storage layer selects these types via the StorageDb/StorageStmt/StorageQuery
// aliases in StorageDbTypes.h.
//
// SQLite is always the source of truth: every method returns SQLite's result and
// SQLite's exceptions propagate unchanged, so the rest of Sourcetrail behaves
// exactly as it does without this mode. Turso runs best-effort against its own
// separate database file (<path>.turso); Turso errors and result mismatches are
// logged as divergences, never thrown.

#include <memory>
#include <string>
#include <string_view>

#include "CppSQLite3.h"
#include "CppTurso3.h"

// Process-wide count of SQLite/Turso divergences observed so far. Lets tests and
// harnesses assert that a run produced none. Reset before a scoped comparison.
namespace turso_compare
{
long long divergenceCount();
void resetDivergenceCount();
}  // namespace turso_compare

class DualQuery
{
public:
	DualQuery();
	DualQuery(
		std::unique_ptr<CppSQLite3Query> sqlite,
		std::unique_ptr<turso::CppTurso3Query> turso,
		bool tursoOk,
		std::string context);

	DualQuery(const DualQuery&) = delete;
	DualQuery& operator=(const DualQuery&) = delete;
	DualQuery(DualQuery&&) = default;
	DualQuery& operator=(DualQuery&&) = default;

	int numFields();
	int fieldDataType(int nCol);
	long long getIntField(int nField, int nNullValue = 0);
	std::string getStringField(int nField, const std::string& szNullValue = "");
	bool eof();
	void nextRow();
	void finalize();

private:
	std::unique_ptr<CppSQLite3Query> m_sqlite;
	std::unique_ptr<turso::CppTurso3Query> m_turso;
	bool m_tursoOk;
	std::string m_context;
};

class DualStatement
{
public:
	DualStatement();
	DualStatement(
		std::unique_ptr<CppSQLite3Statement> sqlite,
		std::unique_ptr<turso::CppTurso3Statement> turso,
		bool tursoOk,
		std::string context);

	DualStatement(const DualStatement&) = delete;
	DualStatement& operator=(const DualStatement&) = delete;
	DualStatement(DualStatement&&) = default;
	DualStatement& operator=(DualStatement&&) = default;

	int execDML();
	DualQuery execQuery();
	void bind(int nParam, std::string_view szValue);
	void bind(int nParam, long long nValue);
	void reset();
	void finalize();

private:
	std::unique_ptr<CppSQLite3Statement> m_sqlite;
	std::unique_ptr<turso::CppTurso3Statement> m_turso;
	bool m_tursoOk;
	std::string m_context;
};

class DualCppSqlite3DB
{
public:
	DualCppSqlite3DB();
	~DualCppSqlite3DB();

	void open(std::string_view szFile);
	void close();
	int execDML(std::string_view szSQL);
	DualQuery execQuery(std::string_view szSQL);
	long long execScalar(std::string_view szSQL, int nNullValue = 0);
	DualStatement compileStatement(std::string_view szSQL);
	long long lastRowId();
	void interrupt();

	// Underlying SQLite handle (the source of truth) so a sqlpp23 connection can
	// borrow it. The Turso side is intentionally not exposed: typed queries run
	// against SQLite only. See CppSQLite3DB::handle and DESIGN_SQLPP23_MIGRATION.md.
	sqlite3 *handle() { return m_sqlite.handle(); }

private:
	DualCppSqlite3DB(const DualCppSqlite3DB&) = delete;
	DualCppSqlite3DB& operator=(const DualCppSqlite3DB&) = delete;

	CppSQLite3DB m_sqlite;
	turso::CppTurso3DB m_turso;
	bool m_tursoOk;
};

#endif	// DUAL_SQLITE_DATABASE_H
