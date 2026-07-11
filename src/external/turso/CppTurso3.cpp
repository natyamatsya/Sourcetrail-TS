////////////////////////////////////////////////////////////////////////////////
// CppTurso3 — implementation. Mirrors CppSQLite3.cpp method-for-method, calling
// the firewalled `tsq_*` C API (turso_core) instead of `sqlite3_*`.
//
// Errors are reported by throwing CppSQLite3Exception (reused from CppSQLite3.h)
// so the storage layer's existing catch/LOG sites behave identically for either
// backend. tsq_last_error() returns a thread-local message; copy on use.
////////////////////////////////////////////////////////////////////////////////
#include "CppTurso3.h"

#include <string>

namespace
{

// tsq_last_error() is a thread-local, never-null C string; safe to pass along.
const char* lastError()
{
	const char* msg = tsq_last_error();
	return msg != nullptr ? msg : "unknown turso error";
}

}  // namespace

namespace turso
{

////////////////////////////////////////////////////////////////////////////////

CppTurso3Query::CppTurso3Query()
{
	mpVM = nullptr;
	mbEof = true;
	mnCols = 0;
	mbOwnVM = false;
}

CppTurso3Query::CppTurso3Query(TsqStmt* pVM, bool bEof, bool bOwnVM /*=true*/)
{
	mpVM = pVM;
	mbEof = bEof;
	mnCols = tsq_column_count(mpVM);
	mbOwnVM = bOwnVM;
}

CppTurso3Query::~CppTurso3Query()
{
	try
	{
		finalize();
	}
	catch (...)
	{
	}
}

int CppTurso3Query::numFields()
{
	checkVM();
	return mnCols;
}

long long CppTurso3Query::getIntField(int nField, int nNullValue /*=0*/)
{
	if (fieldDataType(nField) == TSQ_NULL)
	{
		return nNullValue;
	}
	else
	{
		return tsq_column_int(mpVM, nField);
	}
}

std::string CppTurso3Query::getStringField(int nField, const std::string& szNullValue /*=""*/)
{
	if (fieldDataType(nField) == TSQ_NULL)
	{
		return szNullValue;
	}
	else
	{
		const char* text = tsq_column_text(mpVM, nField);
		return text != nullptr ? std::string(text) : szNullValue;
	}
}

int CppTurso3Query::fieldDataType(int nCol)
{
	checkVM();

	if (nCol < 0 || nCol > mnCols - 1)
	{
		throw CppSQLite3Exception(CPPSQLITE_ERROR, "Invalid field index requested");
	}

	return tsq_column_type(mpVM, nCol);
}

bool CppTurso3Query::eof()
{
	checkVM();
	return mbEof;
}

void CppTurso3Query::nextRow()
{
	checkVM();

	int nRet = tsq_step(mpVM);

	if (nRet == TSQ_DONE)
	{
		mbEof = true;
	}
	else if (nRet == TSQ_ROW)
	{
		// more rows, nothing to do
	}
	else
	{
		tsq_finalize(mpVM);
		mpVM = nullptr;
		throw CppSQLite3Exception(nRet, lastError());
	}
}

void CppTurso3Query::finalize()
{
	if (mpVM && mbOwnVM)
	{
		tsq_finalize(mpVM);
		mpVM = nullptr;
	}
}

void CppTurso3Query::checkVM()
{
	if (mpVM == nullptr)
	{
		throw CppSQLite3Exception(CPPSQLITE_ERROR, "Null Virtual Machine pointer");
	}
}

////////////////////////////////////////////////////////////////////////////////

CppTurso3Statement::CppTurso3Statement()
{
	mpDB = nullptr;
	mpVM = nullptr;
}

CppTurso3Statement::CppTurso3Statement(TsqDb* pDB, TsqStmt* pVM)
{
	mpDB = pDB;
	mpVM = pVM;
}

CppTurso3Statement::CppTurso3Statement(CppTurso3Statement&& rStatement)
{
	mpDB = rStatement.mpDB;
	mpVM = rStatement.mpVM;
	// Only one object can own VM
	rStatement.mpVM = nullptr;
}

CppTurso3Statement& CppTurso3Statement::operator=(CppTurso3Statement&& rStatement)
{
	if (this != &rStatement)
	{
		try
		{
			finalize();
		}
		catch (...)
		{
		}
		mpDB = rStatement.mpDB;
		mpVM = rStatement.mpVM;
		// Only one object can own VM
		rStatement.mpVM = nullptr;
	}

	return *this;
}

CppTurso3Statement::~CppTurso3Statement()
{
	try
	{
		finalize();
	}
	catch (...)
	{
	}
}

int CppTurso3Statement::execDML()
{
	checkDB();
	checkVM();

	int nRet = tsq_step(mpVM);

	if (nRet == TSQ_DONE)
	{
		int nRowsChanged = static_cast<int>(tsq_changes(mpDB));
		tsq_reset(mpVM);
		return nRowsChanged;
	}
	else
	{
		tsq_reset(mpVM);
		throw CppSQLite3Exception(nRet, lastError());
	}
}

CppTurso3Query CppTurso3Statement::execQuery()
{
	checkDB();
	checkVM();

	int nRet = tsq_step(mpVM);

	if (nRet == TSQ_DONE)
	{
		// no rows; statement retains ownership of the VM
		return CppTurso3Query(mpVM, true /*eof*/, false);
	}
	else if (nRet == TSQ_ROW)
	{
		// at least 1 row; statement retains ownership of the VM
		return CppTurso3Query(mpVM, false /*eof*/, false);
	}
	else
	{
		tsq_reset(mpVM);
		throw CppSQLite3Exception(nRet, lastError());
	}
}

void CppTurso3Statement::bind(int nParam, std::string_view szValue)
{
	checkVM();
	int nRes = tsq_bind_text(mpVM, nParam, szValue.data(), static_cast<int>(szValue.length()));

	if (nRes != TSQ_OK)
	{
		throw CppSQLite3Exception(nRes, "Error binding string param");
	}
}

void CppTurso3Statement::bind(int nParam, long long nValue)
{
	checkVM();
	int nRes = tsq_bind_int(mpVM, nParam, nValue);

	if (nRes != TSQ_OK)
	{
		throw CppSQLite3Exception(nRes, "Error binding int param");
	}
}

void CppTurso3Statement::reset()
{
	if (mpVM)
	{
		int nRet = tsq_reset(mpVM);

		if (nRet != TSQ_OK)
		{
			throw CppSQLite3Exception(nRet, lastError());
		}
	}
}

void CppTurso3Statement::finalize()
{
	if (mpVM)
	{
		tsq_finalize(mpVM);
		mpVM = nullptr;
	}
}

void CppTurso3Statement::checkDB()
{
	if (mpDB == nullptr)
	{
		throw CppSQLite3Exception(CPPSQLITE_ERROR, "Database not open");
	}
}

void CppTurso3Statement::checkVM()
{
	if (mpVM == nullptr)
	{
		throw CppSQLite3Exception(CPPSQLITE_ERROR, "Null Virtual Machine pointer");
	}
}

////////////////////////////////////////////////////////////////////////////////

CppTurso3DB::CppTurso3DB()
{
	mpDB = nullptr;
}

CppTurso3DB::~CppTurso3DB()
{
	try
	{
		close();
	}
	catch (...)
	{
	}
}

void CppTurso3DB::open(std::string_view szFile)
{
	// tsq_open needs a NUL-terminated path; string_view may not be.
	std::string file(szFile);
	mpDB = tsq_open(file.c_str());

	if (mpDB == nullptr)
	{
		throw CppSQLite3Exception(TSQ_ERROR, lastError());
	}
}

void CppTurso3DB::close()
{
	if (mpDB)
	{
		tsq_close(mpDB);
		mpDB = nullptr;
	}
}

CppTurso3Statement CppTurso3DB::compileStatement(std::string_view szSQL)
{
	checkDB();

	TsqStmt* pVM = compile(szSQL);
	return CppTurso3Statement(mpDB, pVM);
}

int CppTurso3DB::execDML(std::string_view szSQL)
{
	checkDB();

	std::string sql(szSQL);
	int nRet = tsq_exec(mpDB, sql.c_str());

	if (nRet == TSQ_OK)
	{
		return static_cast<int>(tsq_changes(mpDB));
	}
	else
	{
		throw CppSQLite3Exception(nRet, lastError());
	}
}

CppTurso3Query CppTurso3DB::execQuery(std::string_view szSQL)
{
	checkDB();

	TsqStmt* pVM = compile(szSQL);

	int nRet = tsq_step(pVM);

	if (nRet == TSQ_DONE)
	{
		// no rows; query owns the VM
		return CppTurso3Query(pVM, true /*eof*/);
	}
	else if (nRet == TSQ_ROW)
	{
		// at least 1 row; query owns the VM
		return CppTurso3Query(pVM, false /*eof*/);
	}
	else
	{
		tsq_finalize(pVM);
		throw CppSQLite3Exception(nRet, lastError());
	}
}

long long CppTurso3DB::execScalar(std::string_view szSQL, int nNullValue /*=0*/)
{
	CppTurso3Query q = execQuery(szSQL);

	if (q.eof() || q.numFields() < 1)
	{
		throw CppSQLite3Exception(CPPSQLITE_ERROR, "Invalid scalar query");
	}

	return q.getIntField(0, nNullValue);
}

long long CppTurso3DB::lastRowId()
{
	return tsq_last_insert_rowid(mpDB);
}

void CppTurso3DB::interrupt()
{
	tsq_interrupt(mpDB);
}

void CppTurso3DB::checkDB()
{
	if (!mpDB)
	{
		throw CppSQLite3Exception(CPPSQLITE_ERROR, "Database not open");
	}
}

TsqStmt* CppTurso3DB::compile(std::string_view szSQL)
{
	checkDB();

	std::string sql(szSQL);
	TsqStmt* pVM = tsq_prepare(mpDB, sql.c_str());

	if (pVM == nullptr)
	{
		throw CppSQLite3Exception(TSQ_ERROR, lastError());
	}

	return pVM;
}

}  // namespace turso
