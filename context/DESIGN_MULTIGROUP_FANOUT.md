# Design: Multi-Subprocess Fan-Out Across Source Groups (Phase 8)

**Status: S0 implemented (2026-07-14); S1–S5 designed, not implemented.** Parallelize indexing across source
groups by spawning dedicated subprocess *clusters* per group and routing every group's
results into the already-complete concurrent Turso MVCC writer as the **sole** storage
writer during fan-out.

Decisions (user, 2026-07-11): **per-source-group clusters** + **Turso as the sole writer
for fan-out** (add MVCC conflict-retry first).

Related: [DESIGN_TURSO_BACKEND.md](DESIGN_TURSO_BACKEND.md) (the writer),
[INDEXING_OPTIMIZATIONS.md](INDEXING_OPTIMIZATIONS.md) (throughput measurements),
[ROADMAP_ANALYSIS_ENGINES.md](ROADMAP_ANALYSIS_ENGINES.md) (the storage-seam / Kùzu track).

## Why (and the honest caveats)

Payoff is largest on **skewed multi-group projects** (e.g. one large Rust crate + several
small C++ groups): today all groups pool into one flat command queue drained by
language-typed subprocesses, so a big group can't borrow idle capacity from small ones,
and results funnel through a single serial writer.

Two guardrails the design must respect:

1. **Throughput is not the current bottleneck.** Indexing is measured *parse-bound* — the
   single SQLite writer runs at ~95.5% utilization with **0 back-pressure stalls** (it keeps
   pace). So fan-out + concurrent writes are justified only where fan-out pushes the parse
   rate past the writer. Hence: **opt-in / auto-only-for-multi-group**, and a
   **payoff-measurement gate** (writer stalls should trend to 0; wall-time should drop on a
   skewed load — else keep it off).
2. **The storage *seam* is the durable asset, not Turso.** `ROADMAP_ANALYSIS_ENGINES.md`
   positions Turso as the *proof* a backend swap works via `StorageDbTypes.h` + the
   dual-compare harness, with Kùzu/LadybugDB evaluated to fill "the exact slot SQLite/Turso
   occupy." So route the fan-out write side through a thin writer seam (not
   `ConcurrentTursoWriter` internals) so a Kùzu writer can slot in later. Cross-group dedup
   requires **one shared in-process index** regardless of backend.

## Current state (reviewed; committed at `ebdc4ebd83`)

- **Concurrent Turso writer is complete** (`ConcurrentTursoWriter.{h,cpp}`,
  `ConcurrentStorageIndex.h`): N stdexec-pool threads, one Turso MVCC connection each,
  `BEGIN CONCURRENT`; dedup + id allocation coordinated **in-process** (`AtomicIdCounter` +
  64-shard `ConcurrentInternMap`/`ConcurrentDedupSet`) — correct, because MVCC snapshot
  isolation hides other writers' uncommitted rows so dedup cannot read the DB. All 10 entity
  kinds, 11-table schema. API: `submit(Batch)`, `finish()`, `failedBatches()`.
- **Wired as a mirror**: `TaskInjectStorage::doUpdate` calls `target->inject()` (SQLite) *and*
  `persistent->submitToConcurrentTurso(*source)`; `finishConcurrentTurso()` at
  `TaskFinishParsing::doEnter`.
- **Reads go through real SQLite**: `finish()` does `PRAGMA wal_checkpoint(TRUNCATE)` to a
  SQLite-format `.concurrent.turso` file; the app still reads `.srctrl.db`. (turso_core
  0.7.0-pre.18 reads non-deterministically + SIGBUS on the `tsq_exec`/`prepare_execute_batch`
  path — worked around; see DESIGN_TURSO_BACKEND.md.)
- **No MVCC conflict-retry** (failed batches counted, not retried).
- **Fan-out does not exist**: `CombinedIndexerCommandProvider` pools all groups untagged;
  subprocesses filter by *language type* only.

## Staged design

Each stage is independently buildable/verifiable. Branch from a clean `main` (the in-progress
`ladybug` submodule add on `main` is unrelated — do not sweep it in).

### S0 — MVCC conflict-retry (do first; unblocks "sole writer") — **DONE (2026-07-14)**

Split `ConcurrentTursoWriter::Impl::process` into **resolve** (build `sql` via the intern/remap
loop — unchanged; ids pre-assigned in `ConcurrentStorageIndex`, INSERTs guarded by
`created`/`markNew`) and **commit** (a bounded retry loop around `BEGIN CONCURRENT`/exec/`COMMIT`,
≤8 tries with small backoff; on the conflict code → `ROLLBACK` + retry; count `failedBatches`
only after exhaustion; add `retriedBatches()` instrumentation).

*As implemented:* `tryCommit`/`commitWithRetry` in `ConcurrentTursoWriter.cpp`; the retry
trigger is a new `tsq_last_error_code()` in the turso shim, which classifies turso_core's
`Busy`/`BusySnapshot`/`Conflict`/`WriteWriteConflict`/`CommitDependencyAborted` as `TSQ_BUSY`
(mirroring `SQLITE_BUSY`) — no error-string matching. All INSERTs (graph tables **and**
`filecontent`) are `OR IGNORE`; filecontent step results are now checked. Gate test:
`ConcurrentTursoWriterTestSuite` forces a real write-write conflict between two
`BEGIN CONCURRENT` transactions and asserts TSQ_BUSY classification + idempotent re-run
convergence, alongside the existing serial==concurrent count equivalence.

**Idempotency (the correctness argument):** a conflicted transaction rolls back and commits
**no rows**, so re-running the *identical* `sql` inserts each row for the first time, with the
*same* pre-assigned ids (they came from the in-process index, not DB reads) — no duplicates, no
id drift; the `created`/`markNew` guards ran once in resolve and are not re-evaluated. Harden
against partial-apply with `INSERT OR IGNORE` on **all** graph tables **and `filecontent`**
(PKs are deterministic → a no-op on a clean retry, a safety net otherwise).

### S1 — Tag indexer commands with a source-group id

- `indexer_command.fbs`: append `source_group_id: string;` (FlatBuffers-additive). Regenerate.
- `IndexerCommand.{h,cpp}` base: add `m_sourceGroupId` + get/set; include in `doSerialize` JSON.
- **Tag centrally** at the single choke point `SourceGroup::getIndexerCommandProvider(info)`:
  `for (auto& c : commands) c->setSourceGroupId(getSourceGroupSettings()->getId());` — avoids
  touching the ~8 `getIndexerCommands` subclasses (Cxx CDB / CMakeFileAPI / Empty / Rust /
  Swift / Custom).
- `IndexerCommandSerializer.cpp`: append the field to the positional `CreateIndexerCommand(...)`
  write; `setSourceGroupId(...)` on each read branch.

### S2 — Per-group routing — **single queue + group-id filter** (recommended)

Keep the one SHM queue `icmd_ipc_<uuid>` and its notify/back-pressure; add an **optional
`onlyGroupId`** to `IpcInterprocessIndexerCommandManager::popIndexerCommandBlocking` (empty =
accept any → preserves single-group/legacy behavior). The pop predicate gains
`&& (onlyGroupId.empty() || cmd->getSourceGroupId() == onlyGroupId)` — O(n) like today's
language `skipTypes` scan, negligible vs the per-pop (de)serialize. Subprocess receives its
`onlyGroupId` via a new CLI arg (`TaskBuildIndex::runIndexerProcess` already builds
`commandArguments`; `InterprocessIndexer` passes it into the pop).

*Rejected:* per-group SHM queues (`icmd_ipc_<uuid>_<idx>`) — multiplies segment lifecycle,
complicates the Rust supervisor and `TaskFillIndexerCommandsQueue`. Fallback only if filter
contention shows up. *Trade-off:* a subprocess pinned to a slow group can't steal other groups'
work; S3's proportional allocation + the `""` accept-any fallback mitigate this.

### S3 — Subprocess allocation across groups

Classify enabled non-custom groups into clusters keyed by `(languageType, sourceGroupId)`.
Partition the `indexerThreadCount` budget across **C++ clusters** proportional to each group's
command count, **min 1 per non-empty C++ cluster**; the Rust/Swift supervisors stay one each
(Phase 8's original 3rd bullet — allow K Rust supervisors is a later extension). Carry a
`IndexerClusterPlan { sourceGroupId, language, subprocessCount }` vector from `Project::buildIndex`
into `TaskBuildIndex`; keep ProcessId sequential + a `ProcessId → groupId` map so each supervisor
passes the right `onlyGroupId`. **One group ⇒ one cluster ⇒ today's exact behavior.** Result
routing (`fetchIntermediateStorages`, by ProcessId) is unchanged — group identity is only needed
command-side; all groups feed one writer.

### S4 — Storage cutover: concurrent Turso as sole writer

Route every fetched `IntermediateStorage` to **one shared** writer (shared so
`ConcurrentStorageIndex` dedups a symbol referenced from two groups to one id). Create the writer
**eagerly** in `PersistentStorage::setup()` seeded from `MAX(element.id)+1` (incremental
continuity). In fan-out mode `TaskInjectStorage::doUpdate` **skips `target->inject()`** and calls
`submitToConcurrentTurso` only (gated by a `m_tursoSoleWriter` bool). Keep `TaskMergeStorages`
(fewer, larger batches) and `m_storageProvider`. `finishConcurrentTurso()` stays at
`TaskFinishParsing`.

**The `.turso`↔`.srctrl.db` handoff — export, don't swap (key decision).** The read path, `meta`
table, `updateVersion`, bookmarks, `optimizeMemory`, and `swapToTempStorage` all assume SQLite,
and the Turso schema has no meta/bookmark tables. So after drain, run a **single-threaded
Turso→SQLite export**: read the `.concurrent.turso` tables and bulk-insert into the temp
`SqliteIndexStorage` (empty, so no dedup work — it's done). This keeps the entire read/meta/
bookmark/swap machinery **unchanged**; the Turso writer is purely an ingest accelerator. Reading
directly from Turso (promote `.turso`, run all read queries + meta + bookmarks against
turso_core) is a much larger surface gated on turso read maturity — **deferred**.

### S5 — Gating / fallback

Auto-enable fan-out when `SOURCETRAIL_TURSO_CONCURRENT` is built **and** ≥2 enabled non-custom
groups; else today's exact path. Add a tri-state `ApplicationSettings` override (auto/on/off,
default auto). On `failedBatches() > 0` after retries: log a hard error and either re-run a serial
SQLite inject from the retained `m_storageProvider`, or (simpler first landing) mark the run
degraded via the `TaskFinishParsing` health warning. Interrupt/crash reuses the existing
200-consecutive-failure supervisor per ProcessId; a shared `m_stopSource` stops all clusters (an
incomplete DB is useless anyway).

## Verification

- **Unit:** command source-group-id round-trip; per-group pop filter (`onlyGroupId` selects, `""`
  accepts any); **conflict-retry idempotency** — N writers submitting overlapping batches (shared
  node/edge keys to force MVCC contention) → final node/edge/occurrence counts equal the deduped
  expected set, `failedBatches()==0`, `retriedBatches()` may be >0. This is the core gate for the
  "sole writer" decision.
- **End-to-end smoke** (`scripts/smoke-fanout.sh`, new): index the Sourcetrail repo's own C++
  group **plus** a Rust group with fan-out on; compare against a single-group/serial SQLite
  baseline via `scripts/turso_compare_state.sh` (order-independent per-table content hash).
  Node/edge/occurrence/file/local_symbol **counts must match**; allow the documented node.type
  first-seen-vs-max histogram drift (~0.0025–0.03%, pre-existing).
- **Payoff gate:** report wall-time (fan-out vs serial) on a skewed load and the
  `m_throttleStallCount`/`m_throttleStallMs` from `logIndexingSummary` — fan-out should drive
  stalls down and wall-time down on skew; if a balanced load shows neither, keep it opt-in.
- `scripts/smoke.sh` + `scripts/smoke-rust.sh` stay green (no behavior change without fan-out).

## Top risks

1. **`.turso`↔`.srctrl.db` + meta/bookmark divergence** — the single place "Turso sole writer"
   collides with the SQLite-read assumption. Mitigated by the **Turso→SQLite export** (S4), which
   leaves reads/meta/bookmarks/swap untouched; direct-Turso-reads deferred.
2. **MVCC retry idempotency under partial apply** — pre.18's multi-statement conflict semantics
   aren't fully specified. Mitigated by `INSERT OR IGNORE` on all graph tables + `filecontent`,
   bounded backoff retry, and a degraded-run fallback; guarded by the idempotency unit test.
3. **Cross-group dedup with a shared, incrementally-seeded index** — two groups must not mint
   colliding ids or fail to dedup a shared symbol. Fix: one shared writer instance, eager-created
   in `setup()` seeded from `MAX(id)+1`; guarded by the count comparison vs the serial baseline.
4. **Pre-release engine (turso_core 0.7.0-pre.18) load-bearing at fan-out scale** — keep the
   SQLite single-writer path one flag away; the export model means SQLite still serves all reads.

## Critical files

- `src/lib/data/storage/ConcurrentTursoWriter.{h,cpp}` (retry, `retriedBatches`),
  `ConcurrentStorageIndex.h` (shared instance), `PersistentStorage.{h,cpp}` (eager writer +
  export + seam).
- `src/lib/data/indexer/IndexerCommand.{h,cpp}`; `interprocess/schemas/indexer_command.fbs`;
  `interprocess/serialization/IndexerCommandSerializer.cpp`; `src/lib/project/SourceGroup.cpp`
  (central tag).
- `src/lib/data/indexer/interprocess/IpcInterprocessIndexerCommandManager.{h,cpp}` (group filter);
  `interprocess/InterprocessIndexer.cpp` (CLI arg → pop).
- `src/lib/data/indexer/TaskBuildIndex.cpp` (cluster plan, allocation, ProcessId→group);
  `src/lib/project/Project.cpp` (fan-out selection + cluster plan); `src/lib/data/TaskInjectStorage.cpp`,
  `TaskFinishParsing.cpp` (sole-writer path).
- `scripts/smoke-fanout.sh` (new).

## Priority note

Indexing throughput is not a current bottleneck and is not on `ROADMAP_ANALYSIS_ENGINES.md`'s
near-term list (which leads with in-indexer Datalog for dispatch resolution, then Kùzu behind the
seam once *traversal* is the bottleneck). Phase 8's value is real but narrow (skewed multi-group
loads) — hence the opt-in default and payoff gate. Captured here per the user's explicit decision;
sequence against the analysis-engine track deliberately.
