#include "SqliteStorageMetaSqlpp23.h"

#include <sqlpp23/sqlpp23.h>

#include "MetaTable.h"

namespace meta_poc {
namespace {
constexpr meta::Meta metaTable;

// Minimal model of SQLite's system catalog so hasTable() is a typed query with a
// bound name instead of a string-concatenated one. sqlite_master's schema is
// stable; we only need `type` and `name`.
struct SqliteMaster_ {
  struct Type {
    SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(type, type);
    using data_type = std::optional<::sqlpp::text>;
    using has_default = std::true_type;
  };
  struct Name {
    SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(name, name);
    using data_type = std::optional<::sqlpp::text>;
    using has_default = std::true_type;
  };
  SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(sqlite_master, sqliteMaster);
  template <typename T>
  using _table_columns = sqlpp::table_columns<T, Type, Name>;
  using _required_insert_columns = sqlpp::detail::type_set<>;
};
constexpr ::sqlpp::table_t<SqliteMaster_> sqliteMaster;

template <typename Field>
std::string text(const Field& f) {
  return f.has_value() ? std::string(f.value()) : std::string();
}
}  // namespace

void SqliteStorageMetaSqlpp23::setupMetaTable() {
  // DDL stays raw, routed through the same injected connection (migration hatch).
  m_db(R"(CREATE TABLE IF NOT EXISTS meta(
            id INTEGER, key TEXT, value TEXT, PRIMARY KEY(id)))");
}

void SqliteStorageMetaSqlpp23::clearMetaTable() {
  m_db("DROP TABLE IF EXISTS main.meta");
}

bool SqliteStorageMetaSqlpp23::hasTable(const std::string& tableName) const {
  // The original interpolated tableName straight into the SQL string; here it is
  // a bound comparison against the modelled system catalog.
  for (const auto& row : m_db(select(sqliteMaster.name)
                                  .from(sqliteMaster)
                                  .where(sqliteMaster.type == "table" and
                                         sqliteMaster.name == tableName))) {
    if (text(row.name) == tableName) {
      return true;
    }
  }
  return false;
}

std::string SqliteStorageMetaSqlpp23::getMetaValue(const std::string& key) const {
  if (!hasTable("meta")) {
    return "";
  }
  for (const auto& row : m_db(select(metaTable.value).from(metaTable).where(metaTable.key == key))) {
    return text(row.value);
  }
  return "";
}

void SqliteStorageMetaSqlpp23::insertOrUpdateMetaValue(
    const std::string& key, const std::string& value) {
  // The original emulated an upsert with
  //   INSERT OR REPLACE INTO meta(id,key,value)
  //   VALUES((SELECT id FROM meta WHERE key=?), ?, ?)
  // because the meta table has no UNIQUE(key). The typed, equivalent form is a
  // read-then-update-or-insert. (If the schema gained UNIQUE(key) this could be
  // a single insert_or_replace()/on_conflict(...).do_update(...).)
  bool exists = false;
  for (const auto& row : m_db(select(metaTable.id).from(metaTable).where(metaTable.key == key))) {
    (void)row;
    exists = true;
    break;
  }
  if (exists) {
    m_db(update(metaTable).set(metaTable.value = value).where(metaTable.key == key));
  } else {
    m_db(insert_into(metaTable).set(metaTable.key = key, metaTable.value = value));
  }
}

std::size_t SqliteStorageMetaSqlpp23::getVersion() const {
  const std::string v = getMetaValue("storage_version");
  return v.empty() ? 0 : static_cast<std::size_t>(std::stoi(v));
}

void SqliteStorageMetaSqlpp23::setVersion(std::size_t version) {
  insertOrUpdateMetaValue("storage_version", std::to_string(version));
}

void SqliteStorageMetaSqlpp23::setTime(const std::string& timestamp) {
  insertOrUpdateMetaValue("timestamp", timestamp);
}

std::string SqliteStorageMetaSqlpp23::getTime() const {
  return getMetaValue("timestamp");
}

}  // namespace meta_poc
