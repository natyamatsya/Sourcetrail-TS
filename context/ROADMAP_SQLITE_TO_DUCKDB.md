# Roadmap: Migrating from SQLite to DuckDB

## Executive Summary

This document outlines the strategy and implementation plan for replacing SQLite with DuckDB as the database backend for Sourcetrail. DuckDB offers significant performance improvements for analytical queries while maintaining SQL compatibility and an embedded architecture.

## Current Architecture Analysis

### SQLite Implementation

**Core Components:**
- `SqliteStorage` - Base class for database operations
- `SqliteIndexStorage` - Main index database (symbols, edges, locations)
- `SqliteBookmarkStorage` - Bookmark persistence
- `CppSQLite3` - C++ wrapper around SQLite C API

**Key Files:**
```
src/lib/data/storage/sqlite/
├── SqliteStorage.h/cpp           (Base class, ~6KB)
├── SqliteIndexStorage.h/cpp      (Index storage, ~47KB)
├── SqliteBookmarkStorage.h/cpp   (Bookmarks, ~10KB)
└── SqliteDatabaseIndex.h/cpp     (Index management)

src/external/sqlite/
└── CppSQLite3.h/cpp              (SQLite wrapper)
```

**Database Schema:**
- **Nodes**: Classes, functions, variables (symbols)
- **Edges**: Relationships (calls, inheritance, type use)
- **Source Locations**: File positions for all symbols
- **Occurrences**: Usage locations
- **Errors**: Indexing errors
- **Bookmarks**: User bookmarks

**Key Operations:**
- Bulk inserts during indexing (write-heavy)
- Complex graph queries (joins across nodes/edges)
- Full-text search
- Transaction management
- Prepared statements for performance

## Why DuckDB?

### Performance Benefits
- **10-100x faster** for analytical queries (aggregations, complex joins)
- **Columnar storage** - Better for read-heavy graph traversal
- **Vectorized execution** - SIMD optimizations
- **Better memory management** - Spills to disk efficiently
- **Parallel query execution** - Multi-threaded by default

### Compatibility
- ✅ SQL-compatible (minimal query changes)
- ✅ Embedded (no server needed)
- ✅ ACID transactions
- ✅ Modern C++ API
- ✅ Active development

### Use Case Fit
Perfect for Sourcetrail's workload:
- Complex graph queries with multiple joins
- Large datasets (big codebases)
- Read-heavy after initial indexing
- Analytical aggregations for statistics

## Migration Strategy

### Phase 1: Foundation (Week 1-2)

**Goal:** Set up DuckDB infrastructure alongside SQLite

**Tasks:**
1. **Add DuckDB dependency**
   ```json
   // vcpkg.json
   "dependencies": [
     "duckdb",
     // ... existing
   ]
   ```

2. **Create DuckDB wrapper classes**
   ```
   src/lib/data/storage/duckdb/
   ├── DuckDbStorage.h/cpp           (Base class)
   ├── DuckDbIndexStorage.h/cpp      (Index storage)
   └── DuckDbBookmarkStorage.h/cpp   (Bookmarks)
   ```

3. **Abstract storage interface**
   - Create `IStorage` interface
   - Make both SQLite and DuckDB implement it
   - Allows runtime switching

**Deliverables:**
- DuckDB compiles and links
- Basic connection and transaction support
- Unit tests for core operations

### Phase 2: Schema Migration (Week 3-4)

**Goal:** Port database schema to DuckDB

**Tasks:**
1. **Convert CREATE TABLE statements**
   - Map SQLite types to DuckDB types
   - Preserve indexes and constraints
   - Test schema creation

2. **Implement core operations**
   - `addNode()`, `addEdge()`, `addSourceLocation()`
   - Bulk insert operations
   - Query methods
   - Transaction management

3. **Data migration tool**
   - Read from SQLite database
   - Write to DuckDB database
   - Preserve all relationships
   - Validate data integrity

**Type Mapping:**
```cpp
// SQLite -> DuckDB
INTEGER   -> INTEGER
TEXT      -> VARCHAR
BLOB      -> BLOB
REAL      -> DOUBLE
```

**Deliverables:**
- Complete schema in DuckDB
- All CRUD operations working
- Migration tool for existing databases

### Phase 3: Query Optimization (Week 5-6)

**Goal:** Optimize queries for DuckDB's columnar engine

**Tasks:**
1. **Analyze query patterns**
   - Profile existing SQLite queries
   - Identify performance bottlenecks
   - Measure baseline performance

2. **Optimize for columnar storage**
   - Rewrite queries to leverage vectorization
   - Add appropriate indexes
   - Use DuckDB-specific features (e.g., `COPY`, `PRAGMA`)

3. **Benchmark comparison**
   - SQLite vs DuckDB performance
   - Memory usage comparison
   - Query response times

**Key Queries to Optimize:**
- Graph traversal (finding all callers/callees)
- Full-text search
- Symbol lookup by name
- File dependency queries
- Aggregation queries (statistics)

**Deliverables:**
- Performance benchmarks
- Optimized query implementations
- Documentation of improvements

### Phase 4: Integration (Week 7-8)

**Goal:** Integrate DuckDB into Sourcetrail application

**Tasks:**
1. **Update storage factory**
   ```cpp
   // StorageProvider.cpp
   std::shared_ptr<Storage> createStorage(StorageType type) {
       if (type == DUCKDB) {
           return std::make_shared<DuckDbIndexStorage>(path);
       }
       return std::make_shared<SqliteIndexStorage>(path);
   }
   ```

2. **Add configuration option**
   - User preference for database backend
   - Default to DuckDB for new projects
   - Support legacy SQLite projects

3. **Update project settings**
   - Store database type in `.srctrlprj`
   - Auto-detect database type on load
   - Offer migration on project open

**Deliverables:**
- Working DuckDB integration
- Configuration UI
- Backward compatibility maintained

### Phase 5: Testing & Validation (Week 9-10)

**Goal:** Comprehensive testing and bug fixes

**Tasks:**
1. **Unit tests**
   - All storage operations
   - Transaction handling
   - Error cases
   - Edge cases

2. **Integration tests**
   - Full indexing workflow
   - Query correctness
   - UI responsiveness
   - Large project handling

3. **Migration testing**
   - Convert existing SQLite databases
   - Verify data integrity
   - Performance validation

4. **User acceptance testing**
   - Test with real projects
   - Gather performance feedback
   - Fix reported issues

**Deliverables:**
- Comprehensive test suite
- Bug fixes
- Performance validation report

### Phase 6: Documentation & Rollout (Week 11-12)

**Goal:** Document changes and prepare for release

**Tasks:**
1. **Developer documentation**
   - Architecture changes
   - API documentation
   - Migration guide for contributors

2. **User documentation**
   - Performance improvements
   - Migration instructions
   - Troubleshooting guide

3. **Release preparation**
   - Update CHANGELOG
   - Create migration FAQ
   - Prepare release notes

**Deliverables:**
- Complete documentation
- Release-ready build
- Migration tools

## Implementation Details

### Code Structure

**New Interface:**
```cpp
// IStorage.h
class IStorage {
public:
    virtual ~IStorage() = default;
    
    virtual void setup() = 0;
    virtual void clear() = 0;
    
    virtual Id addNode(const StorageNodeData& data) = 0;
    virtual Id addEdge(const StorageEdgeData& data) = 0;
    // ... other operations
    
    virtual void beginTransaction() = 0;
    virtual void commitTransaction() = 0;
    virtual void rollbackTransaction() = 0;
};
```

**DuckDB Implementation:**
```cpp
// DuckDbStorage.h
#include <duckdb.hpp>

class DuckDbStorage : public IStorage {
public:
    DuckDbStorage(const FilePath& dbFilePath);
    ~DuckDbStorage() override;
    
    void setup() override;
    // ... implement interface
    
protected:
    duckdb::DuckDB m_database;
    duckdb::Connection m_connection;
    FilePath m_dbFilePath;
};
```

### Key Differences to Handle

**1. Prepared Statements**
```cpp
// SQLite (CppSQLite3)
CppSQLite3Statement stmt = m_database.compileStatement(sql);
stmt.bind(1, value);
stmt.execDML();

// DuckDB
auto stmt = m_connection.Prepare(sql);
stmt->Execute(value);
```

**2. Transactions**
```cpp
// SQLite
m_database.execDML("BEGIN TRANSACTION");

// DuckDB
m_connection.BeginTransaction();
```

**3. Bulk Inserts**
```cpp
// DuckDB optimization
m_connection.Query("COPY table FROM 'data.csv'");
// or use Appender for programmatic bulk inserts
duckdb::Appender appender(m_connection, "table");
appender.AppendRow(values...);
appender.Close();
```

### Performance Optimizations

**1. Enable DuckDB Features**
```cpp
// DuckDbStorage.cpp
void DuckDbStorage::enableOptimizations() {
    m_connection.Query("PRAGMA threads=4");
    m_connection.Query("PRAGMA memory_limit='2GB'");
    m_connection.Query("PRAGMA temp_directory='/tmp/sourcetrail'");
}
```

**2. Columnar Indexes**
```sql
-- Create indexes optimized for columnar storage
CREATE INDEX idx_node_name ON node USING ART (name);
CREATE INDEX idx_edge_source_target ON edge (source_node_id, target_node_id);
```

**3. Query Hints**
```sql
-- Use DuckDB's query optimizer hints
SELECT /*+ USE_INDEX(idx_node_name) */ * FROM node WHERE name = ?;
```

## Migration Path for Users

### Automatic Migration
```cpp
// Project.cpp
void Project::load() {
    if (isDatabaseSqlite()) {
        if (userWantsMigration()) {
            migrateToDuckDb();
        }
    }
    // ... continue loading
}
```

### Manual Migration Tool
```bash
# Command-line tool
sourcetrail-migrate --input project.srctrldb --output project.duckdb
```

## Risks & Mitigation

### Risk 1: API Incompatibilities
**Mitigation:** Abstract storage interface, maintain SQLite support

### Risk 2: Performance Regression
**Mitigation:** Comprehensive benchmarking, rollback option

### Risk 3: Data Loss During Migration
**Mitigation:** Backup before migration, validation checks

### Risk 4: User Adoption
**Mitigation:** Gradual rollout, clear documentation, support both backends

## Success Metrics

- **Performance:** 5-10x improvement in query response time
- **Memory:** Similar or better memory usage
- **Reliability:** Zero data loss during migration
- **Adoption:** 80% of users migrate within 6 months
- **Compatibility:** 100% feature parity with SQLite

## Timeline Summary

| Phase | Duration | Key Deliverable |
|-------|----------|-----------------|
| 1. Foundation | 2 weeks | DuckDB infrastructure |
| 2. Schema Migration | 2 weeks | Complete schema + migration tool |
| 3. Query Optimization | 2 weeks | Performance benchmarks |
| 4. Integration | 2 weeks | Working integration |
| 5. Testing | 2 weeks | Comprehensive tests |
| 6. Documentation | 2 weeks | Release-ready |
| **Total** | **12 weeks** | **Production release** |

## Next Steps

1. **Immediate:** Add DuckDB to vcpkg.json
2. **Week 1:** Create DuckDbStorage base class
3. **Week 2:** Port first table (nodes) to DuckDB
4. **Week 3:** Implement bulk insert operations
5. **Week 4:** Create migration tool prototype

## References

- [DuckDB Documentation](https://duckdb.org/docs/)
- [DuckDB C++ API](https://duckdb.org/docs/api/cpp)
- [DuckDB Performance Guide](https://duckdb.org/docs/guides/performance/overview)
- [SQLite to DuckDB Migration](https://duckdb.org/docs/guides/import/sqlite_import)

---

**Document Version:** 1.0  
**Last Updated:** 2026-02-16  
**Author:** Development Team  
**Status:** Draft
