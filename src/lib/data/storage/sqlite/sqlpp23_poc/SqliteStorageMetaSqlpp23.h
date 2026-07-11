#pragma once

// Proof-of-concept: the meta/version/transaction plumbing from SqliteStorage,
// reimplemented on sqlpp23. Same inversion-of-control shape as the bookmark POC
// -- the connection is injected, not owned. In the real refactor this logic
// lives in the SqliteStorage base class shared by every concrete storage.

#include <cstddef>
#include <string>

#include <sqlpp23/sqlite3/database/connection.h>

namespace meta_poc {

class SqliteStorageMetaSqlpp23 {
 public:
  explicit SqliteStorageMetaSqlpp23(sqlpp::sqlite3::connection& db) : m_db(db) {}

  void setupMetaTable();
  void clearMetaTable();
  bool hasTable(const std::string& tableName) const;

  std::string getMetaValue(const std::string& key) const;
  void insertOrUpdateMetaValue(const std::string& key, const std::string& value);

  std::size_t getVersion() const;
  void setVersion(std::size_t version);

  void setTime(const std::string& timestamp);  // real code uses TimeStamp::now()
  std::string getTime() const;

  // Transactions map straight onto the connection's native API.
  void beginTransaction() { m_db.start_transaction(); }
  void commitTransaction() { m_db.commit_transaction(); }
  void rollbackTransaction() { m_db.rollback_transaction(); }

 private:
  sqlpp::sqlite3::connection& m_db;
};

}  // namespace meta_poc
