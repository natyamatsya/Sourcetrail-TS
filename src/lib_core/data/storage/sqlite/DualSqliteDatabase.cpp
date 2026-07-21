// This translation unit is compiled unconditionally (the lib globs its sources),
// so its entire body is gated on the option. When SOURCETRAIL_TURSO_COMPARE is
// off it compiles to an empty object and pulls in no Turso headers.
#ifdef SOURCETRAIL_TURSO_COMPARE

// Module build: LOG_* macros stay textual (macros don't travel through imports); logging.h then
// yields macros only and the backend comes from `import srctrl.logging` below.
#ifdef SRCTRL_MODULE_BUILD
#define SRCTRL_LOGGING_VIA_IMPORT
#endif

#include "DualSqliteDatabase.h"

#include <cctype>
#include <sstream>

#include "logging.h"

// Imports come AFTER all textual #includes (include-before-import rule).
#ifdef SRCTRL_MODULE_BUILD
import srctrl.logging;
#endif

namespace
{

long long g_divergences = 0;

// Turso runs against its own database file so the two engines never share
// storage. Same schema and same operations are applied to both; we compare the
// results they return.
std::string tursoPathFor(std::string_view sqliteFile)
{
	return std::string(sqliteFile) + ".turso";
}

// Statements Turso is known not to support and that do not affect table data
// (PRAGMAs, bare VACUUM). A Turso failure on these is expected and must not be
// counted as a divergence — any behavioural effect still surfaces later in the
// data comparisons. Matches the first keyword, case-insensitively.
bool isKnownUnsupported(std::string_view sql)
{
	size_t i = 0;
	while (i < sql.size() && (sql[i] == ' ' || sql[i] == '\t' || sql[i] == '\n' || sql[i] == '\r'))
	{
		++i;
	}
	const std::string_view rest = sql.substr(i);
	auto startsWithCI = [rest](std::string_view kw) {
		if (rest.size() < kw.size())
		{
			return false;
		}
		for (size_t k = 0; k < kw.size(); ++k)
		{
			if (std::tolower(static_cast<unsigned char>(rest[k])) != kw[k])
			{
				return false;
			}
		}
		return true;
	};
	return startsWithCI("pragma") || startsWithCI("vacuum");
}

// True only for statements where the "rows changed" count is meaningful, i.e.
// SQLite's sqlite3_changes() is defined. After DDL (CREATE/DROP), transaction
// control (BEGIN/COMMIT/ROLLBACK) and the like, the value is unspecified and
// SQLite vs Turso legitimately differ (SQLite carries over a stale count, Turso
// returns 0) — so we must not compare it there.
bool isRowChangingDml(std::string_view sql)
{
	size_t i = 0;
	while (i < sql.size() && (sql[i] == ' ' || sql[i] == '\t' || sql[i] == '\n' || sql[i] == '\r'))
	{
		++i;
	}
	const std::string_view rest = sql.substr(i);
	auto startsWithCI = [rest](std::string_view kw) {
		if (rest.size() < kw.size())
		{
			return false;
		}
		for (size_t k = 0; k < kw.size(); ++k)
		{
			if (std::tolower(static_cast<unsigned char>(rest[k])) != kw[k])
			{
				return false;
			}
		}
		return true;
	};
	return startsWithCI("insert") || startsWithCI("update") || startsWithCI("delete") ||
		startsWithCI("replace");
}

void reportDivergence(const std::string& context, const std::string& detail)
{
	++g_divergences;
	LOG_ERROR("TURSO DIVERGENCE [" + context + "]: " + detail);
}

template <typename T>
void compareValue(const std::string& context, const char* what, const T& sqlite, const T& turso)
{
	if (sqlite != turso)
	{
		std::ostringstream ss;
		ss << what << " differ: sqlite=" << sqlite << " turso=" << turso;
		reportDivergence(context, ss.str());
	}
}

// Run a best-effort Turso operation. On error log a divergence and return false
// (so the caller skips the value comparison for this op), but do NOT disable
// Turso: an isolated failure — e.g. an unsupported PRAGMA or bare VACUUM — must
// not stop comparing everything that follows. SQLite remains the source of truth.
// `tursoEnabled` is taken by value precisely so this cannot flip a caller's flag.
template <typename F>
bool tryTurso(const std::string& context, bool tursoEnabled, F&& fn)
{
	if (!tursoEnabled)
	{
		return false;
	}
	try
	{
		fn();
		return true;
	}
	catch (CppSQLite3Exception& e)
	{
		reportDivergence(context, std::string("turso threw: ") + e.errorMessage());
		return false;
	}
	catch (...)
	{
		reportDivergence(context, "turso threw unknown exception");
		return false;
	}
}

}  // namespace

namespace turso_compare
{
long long divergenceCount()
{
	return g_divergences;
}
void resetDivergenceCount()
{
	g_divergences = 0;
}
}  // namespace turso_compare

////////////////////////////////////////////////////////////////////////////////
// DualQuery

DualQuery::DualQuery(): m_tursoOk(false) {}

DualQuery::DualQuery(
	std::unique_ptr<CppSQLite3Query> sqlite,
	std::unique_ptr<turso::CppTurso3Query> turso,
	bool tursoOk,
	std::string context)
	: m_sqlite(std::move(sqlite))
	, m_turso(std::move(turso))
	, m_tursoOk(tursoOk && m_turso != nullptr)
	, m_context(std::move(context))
{
}

int DualQuery::numFields()
{
	int s = m_sqlite->numFields();
	if (m_tursoOk)
	{
		int t = 0;
		if (tryTurso(m_context, m_tursoOk, [&] { t = m_turso->numFields(); }))
		{
			compareValue(m_context, "numFields", s, t);
		}
	}
	return s;
}

int DualQuery::fieldDataType(int nCol)
{
	int s = m_sqlite->fieldDataType(nCol);
	if (m_tursoOk)
	{
		int t = 0;
		if (tryTurso(m_context, m_tursoOk, [&] { t = m_turso->fieldDataType(nCol); }))
		{
			compareValue(m_context, "fieldDataType", s, t);
		}
	}
	return s;
}

long long DualQuery::getIntField(int nField, int nNullValue)
{
	long long s = m_sqlite->getIntField(nField, nNullValue);
	if (m_tursoOk)
	{
		long long t = 0;
		if (tryTurso(m_context, m_tursoOk, [&] { t = m_turso->getIntField(nField, nNullValue); }))
		{
			compareValue(m_context, "getIntField", s, t);
		}
	}
	return s;
}

std::string DualQuery::getStringField(int nField, const std::string& szNullValue)
{
	std::string s = m_sqlite->getStringField(nField, szNullValue);
	if (m_tursoOk)
	{
		std::string t;
		if (tryTurso(m_context, m_tursoOk, [&] { t = m_turso->getStringField(nField, szNullValue); }))
		{
			compareValue(m_context, "getStringField", s, t);
		}
	}
	return s;
}

bool DualQuery::eof()
{
	bool s = m_sqlite->eof();
	if (m_tursoOk)
	{
		bool t = true;
		if (tryTurso(m_context, m_tursoOk, [&] { t = m_turso->eof(); }))
		{
			compareValue(m_context, "eof", s, t);
		}
	}
	return s;
}

void DualQuery::nextRow()
{
	m_sqlite->nextRow();
	if (m_tursoOk)
	{
		tryTurso(m_context, m_tursoOk, [&] { m_turso->nextRow(); });
	}
}

void DualQuery::finalize()
{
	m_sqlite->finalize();
	if (m_tursoOk)
	{
		tryTurso(m_context, m_tursoOk, [&] { m_turso->finalize(); });
	}
}

////////////////////////////////////////////////////////////////////////////////
// DualStatement

DualStatement::DualStatement(): m_tursoOk(false) {}

DualStatement::DualStatement(
	std::unique_ptr<CppSQLite3Statement> sqlite,
	std::unique_ptr<turso::CppTurso3Statement> turso,
	bool tursoOk,
	std::string context)
	: m_sqlite(std::move(sqlite))
	, m_turso(std::move(turso))
	, m_tursoOk(tursoOk && m_turso != nullptr)
	, m_context(std::move(context))
{
}

int DualStatement::execDML()
{
	int s = m_sqlite->execDML();
	if (m_tursoOk)
	{
		int t = 0;
		if (tryTurso(m_context, m_tursoOk, [&] { t = m_turso->execDML(); }))
		{
			compareValue(m_context, "execDML rows", s, t);
		}
	}
	return s;
}

DualQuery DualStatement::execQuery()
{
	// Reads are SQLite-only — see DualCppSqlite3DB::execQuery.
	auto sq = std::unique_ptr<CppSQLite3Query>(new CppSQLite3Query(m_sqlite->execQuery()));
	return DualQuery(std::move(sq), nullptr, false, m_context);
}

void DualStatement::bind(int nParam, std::string_view szValue)
{
	m_sqlite->bind(nParam, szValue);
	if (m_tursoOk)
	{
		tryTurso(m_context, m_tursoOk, [&] { m_turso->bind(nParam, szValue); });
	}
}

void DualStatement::bind(int nParam, long long nValue)
{
	m_sqlite->bind(nParam, nValue);
	if (m_tursoOk)
	{
		tryTurso(m_context, m_tursoOk, [&] { m_turso->bind(nParam, nValue); });
	}
}

void DualStatement::reset()
{
	m_sqlite->reset();
	if (m_tursoOk)
	{
		tryTurso(m_context, m_tursoOk, [&] { m_turso->reset(); });
	}
}

void DualStatement::finalize()
{
	m_sqlite->finalize();
	if (m_tursoOk)
	{
		tryTurso(m_context, m_tursoOk, [&] { m_turso->finalize(); });
	}
}

////////////////////////////////////////////////////////////////////////////////
// DualCppSqlite3DB

DualCppSqlite3DB::DualCppSqlite3DB(): m_tursoOk(false) {}

DualCppSqlite3DB::~DualCppSqlite3DB() = default;

void DualCppSqlite3DB::open(std::string_view szFile)
{
	// SQLite is the source of truth: let its exception propagate.
	m_sqlite.open(szFile);

	const std::string context = "open";
	m_tursoOk = true;
	m_tursoOk = tryTurso(context, m_tursoOk, [&] { m_turso.open(tursoPathFor(szFile)); });
	if (!m_tursoOk)
	{
		LOG_WARNING("Turso comparison disabled: failed to open turso database for " + std::string(szFile));
	}
}

void DualCppSqlite3DB::close()
{
	m_sqlite.close();
	if (m_tursoOk)
	{
		tryTurso("close", m_tursoOk, [&] { m_turso.close(); });
	}
}

int DualCppSqlite3DB::execDML(std::string_view szSQL)
{
	const std::string context = "execDML: " + std::string(szSQL);
	int s = m_sqlite.execDML(szSQL);
	if (m_tursoOk)
	{
		try
		{
			int t = m_turso.execDML(szSQL);
			// Only meaningful for DML; DDL/BEGIN/COMMIT leave changes() unspecified.
			if (isRowChangingDml(szSQL))
			{
				compareValue(context, "execDML rows", s, t);
			}
		}
		catch (CppSQLite3Exception& e)
		{
			if (isKnownUnsupported(szSQL))
			{
				// Expected: Turso rejects some PRAGMAs / bare VACUUM by design.
				LOG_INFO("Turso skipped unsupported statement [" + context + "]");
			}
			else
			{
				reportDivergence(context, std::string("turso threw: ") + e.errorMessage());
			}
		}
	}
	return s;
}

DualQuery DualCppSqlite3DB::execQuery(std::string_view szSQL)
{
	// Reads are SQLite-only. Per-row comparison of these queries is unsound: the
	// engines legitimately return the same rows in a different physical order
	// (no ORDER BY), and reads during indexing observe different intermediate
	// states. Data equivalence is verified authoritatively by whole-table set
	// comparison at the end (see scripts/turso_compare_state.sh) — SQLite remains
	// the source of truth for the values the caller receives.
	const std::string context = "execQuery: " + std::string(szSQL);
	auto sq = std::unique_ptr<CppSQLite3Query>(new CppSQLite3Query(m_sqlite.execQuery(szSQL)));
	return DualQuery(std::move(sq), nullptr, false, context);
}

long long DualCppSqlite3DB::execScalar(std::string_view szSQL, int nNullValue)
{
	// Reuse execQuery so the comparison happens through DualQuery.
	DualQuery q = execQuery(szSQL);

	if (q.eof() || q.numFields() < 1)
	{
		throw CppSQLite3Exception(CPPSQLITE_ERROR, "Invalid scalar query");
	}

	return q.getIntField(0, nNullValue);
}

DualStatement DualCppSqlite3DB::compileStatement(std::string_view szSQL)
{
	const std::string context = "stmt: " + std::string(szSQL);
	auto ss = std::unique_ptr<CppSQLite3Statement>(new CppSQLite3Statement(m_sqlite.compileStatement(szSQL)));

	std::unique_ptr<turso::CppTurso3Statement> ts;
	bool tok = false;
	if (m_tursoOk)
	{
		tok = tryTurso(context, m_tursoOk, [&] {
			ts = std::unique_ptr<turso::CppTurso3Statement>(
				new turso::CppTurso3Statement(m_turso.compileStatement(szSQL)));
		});
	}
	return DualStatement(std::move(ss), std::move(ts), tok, context);
}

long long DualCppSqlite3DB::lastRowId()
{
	long long s = m_sqlite.lastRowId();
	if (m_tursoOk)
	{
		long long t = 0;
		if (tryTurso("lastRowId", m_tursoOk, [&] { t = m_turso.lastRowId(); }))
		{
			compareValue("lastRowId", "lastRowId", s, t);
		}
	}
	return s;
}

void DualCppSqlite3DB::interrupt()
{
	m_sqlite.interrupt();
	if (m_tursoOk)
	{
		tryTurso("interrupt", m_tursoOk, [&] { m_turso.interrupt(); });
	}
}

#endif  // SOURCETRAIL_TURSO_COMPARE
