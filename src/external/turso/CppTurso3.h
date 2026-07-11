////////////////////////////////////////////////////////////////////////////////
// CppTurso3 — a C++ wrapper around the `turso_shim` C API (turso_core).
//
// A method-for-method mirror of CppSQLite3DB/Statement/Query, but calling the
// firewalled `tsq_*` functions instead of `sqlite3_*`. Lives in `namespace turso`
// so both engines can coexist in one binary; the real firewall is the `tsq_`
// symbol prefix, the namespace is the C++ ergonomics.
//
// Reuses CppSQLite3Exception (from CppSQLite3.h) so the storage layer's existing
// catch/LOG sites work identically for either backend.
////////////////////////////////////////////////////////////////////////////////
#ifndef CPP_TURSO3_H
#define CPP_TURSO3_H

#include "CppSQLite3.h"  // for CppSQLite3Exception, reused verbatim
#include "turso_shim.h"

#include <string>
#include <string_view>

namespace turso
{

class CppTurso3Query
{
public:
	CppTurso3Query();
	CppTurso3Query(TsqStmt* pVM, bool bEof, bool bOwnVM = true);

	CppTurso3Query(const CppTurso3Query&) = delete;
	CppTurso3Query& operator=(const CppTurso3Query&) = delete;

	virtual ~CppTurso3Query();

	int numFields();

	int fieldDataType(int nCol);

	long long getIntField(int nField, int nNullValue = 0);
	std::string getStringField(int nField, const std::string& szNullValue = "");

	bool eof();

	void nextRow();

	void finalize();

private:
	void checkVM();

	TsqStmt* mpVM;
	bool mbEof;
	int mnCols;
	bool mbOwnVM;
};

class CppTurso3Statement
{
public:
	CppTurso3Statement();
	CppTurso3Statement(TsqDb* pDB, TsqStmt* pVM);

	CppTurso3Statement(CppTurso3Statement&&);
	CppTurso3Statement& operator=(CppTurso3Statement&&);

	virtual ~CppTurso3Statement();

	int execDML();

	CppTurso3Query execQuery();

	void bind(int nParam, std::string_view szValue);
	void bind(int nParam, long long nValue);

	void reset();

	void finalize();

private:
	void checkDB();
	void checkVM();

	TsqDb* mpDB;
	TsqStmt* mpVM;
};

class CppTurso3DB
{
public:
	CppTurso3DB();

	virtual ~CppTurso3DB();

	void open(std::string_view szFile);

	void close();

	int execDML(std::string_view szSQL);

	CppTurso3Query execQuery(std::string_view szSQL);

	long long execScalar(std::string_view szSQL, int nNullValue = 0);

	CppTurso3Statement compileStatement(std::string_view szSQL);

	long long lastRowId();

	void interrupt();

private:
	CppTurso3DB(const CppTurso3DB&) = delete;
	CppTurso3DB& operator=(const CppTurso3DB&) = delete;

	void checkDB();

	TsqStmt* compile(std::string_view szSQL);

	TsqDb* mpDB;
};

}  // namespace turso

#endif  // CPP_TURSO3_H
