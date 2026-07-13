# Turso storage backend — design & the subtle deviations from SQLite

Sourcetrail persists its index in SQLite. This document describes the optional
Turso ([tursodatabase/turso](https://github.com/tursodatabase/turso), a Rust
re-implementation of SQLite) storage paths added alongside it, and — most
importantly — the **subtle, easy-to-forget ways the Turso result deviates from
the SQLite result**. Read the deviations section before trusting a Turso graph
as byte-equivalent to SQLite.

Pinned engine: `turso_core = "=0.7.0-pre.18"`. Several notes below are specific
to that pre-release.

## Modes (all opt-in, off by default)

| CMake option | What it does |
|---|---|
| `SOURCETRAIL_TURSO_COMPARE` | Dual backend: run SQLite and Turso in lockstep, diff results. SQLite is the source of truth. |
| `SOURCETRAIL_CLIENT_IDS` | Assign element ids from an in-process counter (no `INSERT`+`lastRowId()` round-trip). Precondition for concurrency. |
| `SOURCETRAIL_TURSO_CONCURRENT` | Turso-only concurrent-writer inject path: N MVCC writers commit in parallel, piggybacking the live indexer. |

The firewall that lets Turso link next to real SQLite: `src/turso_shim` re-exports
the ~20 sqlite3 C-API primitives Sourcetrail uses under a `tsq_` prefix
(`nm` confirms 0 colliding `sqlite3_*` symbols). C++ wrapper: `src/external/turso`.

## Deviations from SQLite (the subtle ones)

### 1. `node.type`: first-seen vs. max — a ~0.0025–0.03% difference

The single known **data** deviation of the concurrent path.

- SQLite (`SqliteIndexStorage::addNodes`) dedups a node by `serialized_name` and,
  on re-add, **upgrades** its type to the numeric max (`if (stored < new) setNodeType(...)`).
  A forward declaration (low `NodeKind` bit) later seen as a definition (higher
  bit) ends up at the definition's type.
- The concurrent writer dedups by name too, but **stores the first-seen type**
  (whichever of the N parallel writers creates the node wins the race). So a
  small number of forward-decl-then-definition nodes keep the lower type.
- Observed via histogram diff (`node.type`):
  - ripgrep (52K LOC): **0 divergence** (no cross-batch upgrades).
  - tokio (159K LOC): ~3–10 nodes of 35,421.
  - rust-analyzer (483K LOC): **1 node of 40,016** (0.0025%).
- `ConcurrentStorageIndex`/`ConcurrentNodeIndex` **tracks the max type** and
  exposes `forEachNode(id, maxType)` for a future reconciliation. A finish-time
  `UPDATE node SET type=max` pass was implemented and **reverted**: under turso
  0.7.0-pre.18 MVCC it over-corrected and read non-deterministically (raised more
  nodes than SQLite and the result varied run-to-run). Fixing this correctly is
  the one open correctness gap. `symbol.definition_kind` has the same shape but
  was **identical** on all three projects, so it is left as-is.

### 2. MVCC data is not on disk until you checkpoint

Committed `BEGIN CONCURRENT` transactions live in turso's **in-memory MVCC store
+ logical log** (`<db>-log`). On close the `.turso` file is still an 8 KB schema
stub — the data is lost unless you flush it.

- **Flush with `PRAGMA wal_checkpoint(TRUNCATE)`** (or `Connection::checkpoint(
  CheckpointMode::Truncate)`): it materializes the log into the main db file and
  truncates the log. `ConcurrentTursoWriter::finish()` does this after draining;
  rust-analyzer's file then grows to ~54 MB and survives a fresh reopen.
- **GOTCHA:** the checkpoint must be driven via **prepare+step** (`tsq_prepare` +
  `tsq_step` loop). Routing it through `tsq_exec` (which uses
  `prepare_execute_batch`) **SIGBUS-crashes** turso 0.7.0-pre.18. (`catch_unwind`
  does not catch a hardware signal, so it takes the whole process down.)
- MVCC keeps a `<db>-log` sidecar; clean it up between runs or a stale log +
  WAL-mode header reads as "database is corrupt".

### 3. A checkpointed MVCC file is **not** SQLite-readable

Unlike the WAL-mode `.turso` files the dual-compare mode produces (which `sqlite3`
can open), the **MVCC-checkpointed main file uses turso's own on-disk format**.
`sqlite3 file` reports "file is not a database". Reopen it via the shim / a
turso_core reader (`mvcc_probe verify`/`compare`).

### 4. Absolute ids are a run-dependent permutation — even in stock SQLite

`node`/`edge`/`occurrence` ids differ between two runs of *the same* engine
(parallel parse + macro-expansion order affects the order of first-seen, hence id
assignment). `element`/`symbol`/`source_location`/`local_symbol`/`filecontent`
are stable. Consequence: **you cannot compare raw rows** (they embed ids). Compare
either whole-table *content sets* (dual mode, same op stream to both engines →
`scripts/turso_compare_state.sh`) or **value-distribution histograms** across runs
(`mvcc_probe compare`). Per-op lockstep comparison of *reads* is also unsound
(unordered `SELECT`s return the same rows in a different physical order); the dual
backend therefore compares only writes per-op and defers to a final set compare.

### 5. `element_component` ids come from the shared counter

SQLite gives `element_component` its own per-table autoincrement; the concurrent
writer mints its id from the shared element counter, so those ids differ (the rows
are otherwise equal). Moot in practice — Rust indexing produces **0**
element_components. Revisit if a project produces them.

### 6. Foreign keys must be OFF for concurrent writers

Under MVCC snapshot isolation a writer cannot see another writer's *uncommitted*
node, so a concurrent edge/occurrence referencing it would fail an FK check.
Sourcetrail normally sets `PRAGMA FOREIGN_KEYS=ON`; the concurrent schema is built
**without FK constraints** (Sourcetrail validates/rebuilds structure afterward).

### 7. Statements Turso rejects by design (dual mode)

`PRAGMA MMAP_SIZE` is ignored, `JOURNAL_MODE=WAL` is partial, and bare `VACUUM`
is unsupported (only `VACUUM INTO`). The dual backend treats a Turso failure on a
leading `PRAGMA`/`VACUUM` as **expected, not a divergence**. It also only compares
`execDML` row counts for `INSERT/UPDATE/DELETE/REPLACE` — after DDL/`BEGIN`,
`changes()` is unspecified and legitimately differs (SQLite carries a stale count,
Turso returns 0).

### 8. `turso_core::types::Numeric` is private

Build/read values only through `Value`'s public API (`from_i64`, `value_type()`,
`as_int()`, `to_float_or_zero()`, `to_text()`, `to_blob()`), never by naming
`Numeric`.

## Verification methodology

- `mvcc_probe compare <dbA> <dbB>` — opens both (turso_core reads turso-MVCC *and*
  sqlite), diffs raw counts + histograms. The primary equivalence check.
- `scripts/turso_compare_state.sh <sqlite.db> <turso.db>` — whole-table set
  comparison (dump → sort → hash), for the dual/WAL-mode files.
- `mvcc_probe verify <path> <table>` — reopen an MVCC file in a fresh process.
- Unit tests: `ConcurrentStorageIndexTestSuite` (race-free allocator, TSan-clean,
  max-type tracking), `ConcurrentTursoWriterTestSuite` (all kinds, concurrent ==
  serial), `TursoCompareTestSuite` (dual equivalence).

**Equivalence proven end-to-end** on ripgrep (52K LOC), tokio (159K), and
rust-analyzer (483K): all table counts + all value histograms match, **except the
`node.type` first-seen deviation (§1)**. The concurrent writer committed with 0
failed batches at 713K elements and the 54 MB result survived a fresh reopen.

## Reproduce

```sh
# concurrent path, alongside plain SQLite:
cmake --preset llvm-clang-dbg -DSOURCETRAIL_TURSO_CONCURRENT=ON
cmake --build .build/llvm-clang-dbg --target Sourcetrail copy_rust_indexer
# index a Rust project; writer db is <sqlitedb>.concurrent.turso (on the temp db)
(cd .build/llvm-clang-dbg/app && ./Sourcetrail index --full <proj>/index.srctrl.toml)
# compare against the SQLite graph:
src/turso_shim/target/debug/mvcc_probe compare \
    <proj>/index.srctrl.db.tmp.concurrent.turso <proj>/index.srctrl.db
```
