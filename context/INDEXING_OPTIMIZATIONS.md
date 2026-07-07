# Indexing optimization opportunities

- **Status:** living document — priorities agreed 2026-07-07
- **Measurement setup:** headless CLI `Sourcetrail index --full Sourcetrail.srctrl.toml`
  (this repo, ~548 C++ files), 12-core (8P+4E) / 32 GB Apple Silicon, debug build.
  Numbers come from the permanent instrumentation added 2026-07-07 (`indexing summary`,
  `storage writer`, `storage pre-merge` log lines).

## Measured state (2026-07-07)

| Metric | cap 6 | cap 12 / uncapped |
|---|---|---|
| Full-index wall time | 163 s | **128 s (−21 %)** |
| Storage-writer utilization | 82 % | 95.5 % |
| Writer back-pressure stalls | 0 | **0** |
| Pre-merge | 113 merges / 4.1 s | 300 merges / 14.8 s |
| Serial writer workload | 434 injects / 4.06 M locations | 248 injects / **2.56 M** locations |
| Peak RSS (app + indexers) | — | 8.4 GB (~0.8 GB per clang frontend) |

Interpretation: full indexing is **parse-bound**; the serial SQLite writer keeps pace
(zero stalls) but sits near saturation. The in-memory pre-merge absorbs producer
throughput and pre-deduplicates, shrinking the serial tail by ~37 %.

## What already exists (the caching layers)

1. **Incremental refresh (default ON).** `RefreshMode::UPDATED_FILES` is the default
   (CLI `--full` opts out). `Project::buildIndex` seeds the temp DB from the live DB and
   only clears + re-indexes changed files; invalidation propagates through the stored
   include graph (`PersistentStorage::getReferencing`, RefreshInfoGenerator.cpp:81).
2. **Per-source-group PCH (opt-in).** `SourceGroupSettingsWithCxxPchOptions` →
   `createBuildPchTask` (utilitySourceGroupCxx.cpp) generates Sourcetrail's own PCH as a
   pre-index task; project `-include-pch` flags are deliberately stripped (a project's
   `.pch` is compiler-version-locked and unusable by our LLVM).
3. **Within-run dedup.** `FileRegister`/`CanonicalFilePathCache` avoid re-*recording*
   headers already indexed this run (clang still re-*parses* them per TU — only PCH
   avoids that).
4. **CMake File API integration.** `CMakeFileAPIReader` runs CMake itself when needed
   and parses `codemodel-v2` + `cmakeFiles-v1` + `toolchains-v1`: per-TU includes,
   defines, and full compile-command fragments.

## Opportunities (priority order)

### P0 — Baseline the incremental path (measurement, ~zero effort)
Touch one `.cpp`, re-index **without** `--full`, record the delta with the existing
instrumentation. Informs P1/P3 and documents the cache we already have.
**Payoff:** decision data. **Risk:** none.

### P1 — Compile-flag-aware refresh (correctness fix disguised as perf) ← agreed first
**Problem:** `RefreshInfoGenerator` compares only `lastWriteTime`
(RefreshInfoGenerator.cpp:237). A `CMakeLists.txt` change (new define/include/standard)
alters compile commands without touching source mtimes → `UPDATED_FILES` re-indexes
nothing → **silently stale index** unless the user knows to run `--full`.
**Approach:** we already hold the exact per-TU command line from the File API. Store a
per-file **compile-command hash** in the DB (schema: new column or meta k/v per file);
`RefreshInfoGenerator` treats hash-mismatch exactly like mtime-change (per-TU precision:
only TUs whose flags moved re-index).
**Payoff:** incremental refresh becomes trustworthy — the precondition for relying on it
on large code bases. **Effort:** moderate (schema + generator + File API plumb-through).
**Risk:** low; worst case an unnecessary re-index.

### P2 — Zero-config PCH from the codemodel (+ freshness check)
**Problem:** PCH is the only lever against per-TU header re-parsing, but it needs manual
per-source-group setup, and `createBuildPchTask` regenerates unconditionally every run.
**Approach:** parse the `precompileHeaders` field of codemodel-v2 compile groups (the
reader currently drops it); auto-feed the existing PCH machinery when a project uses
`target_precompile_headers`. Add a freshness check (PCH input mtime + flags hash vs the
existing `.pch`) so unchanged PCHs are reused across runs.
**Payoff:** large parse-time win on header-heavy projects, zero config. **Effort:**
moderate (reader schema + settings plumbing + freshness). **Risk:** low (falls back to
no-PCH behavior).

### P3 — Content-hash change detection
**Problem:** mtime-based change detection makes `git checkout` / branch switches trigger
near-full re-indexes even when file contents are identical.
**Payoff:** big for branch-switching workflows on large repos. **Approach:** store a
content hash per file (compute during indexing when the file is read anyway); refresh
compares hash when mtime differs (mtime as a cheap first-pass filter, hash as the
decider). **Effort:** moderate. **Risk:** low; orthogonal to CMake (also helps CDB and
plain source groups).

### P4 — Sharded SQLite ingest (B3) — GATED, do not build yet
**Problem:** the serial writer is the eventual throughput ceiling (single `m_dataMutex`
+ one SQLite txn per inject; global element-ID space; name-based dedup).
**Feasibility (explored):** two-phase ingest sharding works — K parallel shard DBs (each
deduping internally in its in-memory name index), merged into the temp DB via the
existing `Storage::inject`; precedent in-tree (`TaskExecuteCustomCommands` per-thread
DBs). `ATTACH`/ID-offset merging is NOT viable (dedup is name-based, IDs are global).
Shard-side pre-dedup shrinks the serial tail — the 2026-07-07 measurement demonstrated
the mechanism (pre-merge cut writer workload 37 %).
**Gate:** the `indexing summary` log's **`writer back-pressure stalls`** counter.
Stalls > 0 on a real workload = producers outran the writer = B3 starts paying.
Currently 0 even with 12 producers. **Effort:** high. **Risk:** medium (data-critical
path).

### P4b — Distributed sharded indexing (SHIPPED 2026-07-07)
Reframed from local wall-time (P4, still gated) to **fanning a large codebase across
machines**: N producers each index a deterministic stripe of the sorted TU set into a
standalone shard DB; one `merge` command combines them. Each producer is the *unmodified*
single-writer pipeline in its own process, so there is no concurrent-writer redesign —
the parallelism is across processes/machines, not writer lanes.

- **Producer:** `Sourcetrail index --full --shard <i>/<N> [--shard-output <db>]`.
  `shard::stripeFilter` (ShardConfig.h) keeps TUs whose position in the sorted
  `RefreshInfo::filesToIndex` set satisfies `pos % N == i-1`. `Project::buildIndex` writes
  the shard DB at the output path with an empty bookmark DB and skips the swap/discard step;
  a manifest (`shard_index`/`shard_count`/`shard_file_count`) is written to the meta table.
- **Merge:** `Sourcetrail merge <project.toml> <shard1.db> <shard2.db>... [--output <db>] [--allow-partial]`.
  Guards version compatibility + a complete, distinct stripe set, then `inject`s each shard
  into a fresh target and finalizes (project-settings meta, `buildCaches`, `optimizeMemory`).
- **Cross-shard dedup is automatic** at inject: nodes by serialized name, source locations by
  position, and occurrences via the occurrence table's composite PK + `INSERT OR IGNORE`.
  (The plan expected occurrences to need a post-merge dedup pass; the composite PK already
  handles it — verified, no dedup pass shipped.)
- **v1 constraints:** shard runs use `--full` (ALL_FILES); all producers + the merge host share
  the same checkout root (paths are stored absolute, no remap); same storage version.
- **Acceptance:** `scripts/smoke-distributed.sh` proves `shard 1/2 + shard 2/2 + merge` equals a
  direct full index on all of node/edge/file/source_location/occurrence counts, using a CDB
  fixture whose two TUs share a header (so the merge genuinely collapses overlapping shard rows
  — 8+9 shard nodes → 11 merged). `ShardConfigTestSuite` covers stripe determinism/disjointness/
  completeness/balance.
- **Follow-ups:** path remapping (index and merge on different roots), size-balanced striping
  (by TU cost, not position), and a parallel merge tree for very large N.

### P5 — Not worth it (recorded so we don't re-litigate)
- **Ninja `.ninja_deps` for invalidation:** our stored include graph is equivalent after
  the first index.
- **Parallel writers into one SQLite / ATTACH-merge:** engine + ID-scheme constraints
  (see P4 feasibility).
- **Raising the indexer subprocess cap:** done — cap removed entirely (2026-07-07); the
  setting / hw-concurrency auto is the knob; back-pressure self-regulates.

## Standing infrastructure
- `scripts/smoke.sh` — unit subset + headless tutorial index + SIGINT interrupt stage.
- Permanent per-run summaries: `indexing summary` (wall, files, stalls), `storage writer`
  (injects, locations, busy ms), `storage pre-merge` (merges, busy ms).
- `SYNCHRONOUS=OFF` on the throwaway indexing DB (restored before optimize/swap).
