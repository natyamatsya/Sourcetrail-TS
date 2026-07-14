# Design: Multi-Subprocess Fan-Out Across Source Groups (Phase 8)

**Status: COMPLETE — S0–S5 implemented (2026-07-14).** Remaining follow-ups: payoff
measurement on a genuinely skewed real-world load (the smoke fixture is balanced and
too small to be meaningful), sole-writer support for incremental refreshes (needs
content-dedup seeding of the in-process index, not just id seeding), the serial
re-inject fallback for degraded runs (currently: health warning + manual serial re-run),
and the Rust analog — K supervisors draining the per-crate command queue
([DESIGN_RUST_CRATE_FANOUT.md](DESIGN_RUST_CRATE_FANOUT.md)). Parallelize indexing across source
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

### S1 — Tag indexer commands with a source-group id — **DONE (2026-07-14)**

- `indexer_command.fbs`: append `source_group_id: string;` (FlatBuffers-additive). Regenerate.
- `IndexerCommand.{h,cpp}` base: add `m_sourceGroupId` + get/set; include in `doSerialize` JSON.
- **Tag centrally** at the single choke point `SourceGroup::getIndexerCommandProvider(info)`:
  `for (auto& c : commands) c->setSourceGroupId(getSourceGroupSettings()->getId());` — avoids
  touching the ~8 `getIndexerCommands` subclasses (Cxx CDB / CMakeFileAPI / Empty / Rust /
  Swift / Custom).
- `IndexerCommandSerializer.cpp`: append the field to the positional `CreateIndexerCommand(...)`
  write; `setSourceGroupId(...)` on each read branch.

*As implemented — one deviation from the sketch above:* `SourceGroup::getIndexerCommandProvider`
is NOT a single choke point (the three C++ groups override it with lazy
`CxxIndexerCommandProvider`s whose commands only materialize on consume). The tag therefore
lives on the **consumer** side: `CombinedIndexerCommandProvider::addProvider(provider, groupId)`
(fed by `Project::buildIndex` via a new public `SourceGroup::getSourceGroupId()`) tags every
command in its consume paths — still one place, and it covers lazy providers. Additionally the
**Rust and Swift supervisors' pop-rewrite** paths were made lossless for the new field (their
`OwnedIndexerCommand` copies rewrite the whole queue on every pop; a missing field there
silently strips it from remaining commands — the Swift copy was already dropping the Rust
cargo fields, fixed alongside). Gate tests: fbs round-trip (`IpcSerializerTestSuite`),
end-to-end tag-through-SHM-queue (`IpcIntegrationTestSuite`), Rust pop-rewrite preservation
(`command.rs` unit test). Note: the Swift package is not compiled in the dev build
(`BUILD_SWIFT_LANGUAGE_PACKAGE=OFF`) and its checked-in channel code predates the current
flatc Swift codegen API — it needs a compile pass when Swift is next enabled.

### S2 — Per-group routing — **single queue + group-id filter** (recommended) — **DONE (2026-07-14)**

Keep the one SHM queue `icmd_ipc_<uuid>` and its notify/back-pressure; add an **optional
`onlyGroupId`** to `IpcInterprocessIndexerCommandManager::popIndexerCommandBlocking` (empty =
accept any → preserves single-group/legacy behavior). The pop predicate gains
`&& (onlyGroupId.empty() || cmd->getSourceGroupId() == onlyGroupId)` — O(n) like today's
language `skipTypes` scan, negligible vs the per-pop (de)serialize. Subprocess receives its
`onlyGroupId` via a new CLI arg (`TaskBuildIndex::runIndexerProcess` already builds
`commandArguments`; `InterprocessIndexer` passes it into the pop).

*As implemented:* filter in `tryPopLocked` (shared by polling and blocking pops);
`InterprocessIndexer` ctor takes `onlyGroupId` (default empty). The CLI arg is flag-style —
`--only-group-id=<id>` — parsed out **before** the positional arguments in the indexer's
`main()`, because the positional layout has an optional trailing `logFilePath` that an
appended positional would collide with. `TaskBuildIndex` holds a `ProcessId → groupId` map
(`m_processGroupIds`) consulted in `runIndexerProcess`; it stays empty until S3's cluster
plan populates it, so behavior today is exactly legacy. Gate test:
`IpcIntegrationTestSuite` "per-group command pop filter" (pinned pop selects across
queue order, drained group returns null leaving others untouched, `""` accepts any).

*Rejected:* per-group SHM queues (`icmd_ipc_<uuid>_<idx>`) — multiplies segment lifecycle,
complicates the Rust supervisor and `TaskFillIndexerCommandsQueue`. Fallback only if filter
contention shows up. *Trade-off:* a subprocess pinned to a slow group can't steal other groups'
work; S3's proportional allocation + the `""` accept-any fallback mitigate this.

### S3 — Subprocess allocation across groups — **DONE (2026-07-14)**

Classify enabled non-custom groups into clusters keyed by `(languageType, sourceGroupId)`.
Partition the `indexerThreadCount` budget across **C++ clusters** proportional to each group's
command count, **min 1 per non-empty C++ cluster**; the Rust/Swift supervisors stay one each
(Phase 8's original 3rd bullet — allow K Rust supervisors is a later extension). Carry a
`IndexerClusterPlan { sourceGroupId, language, subprocessCount }` vector from `Project::buildIndex`
into `TaskBuildIndex`; keep ProcessId sequential + a `ProcessId → groupId` map so each supervisor
passes the right `onlyGroupId`. **One group ⇒ one cluster ⇒ today's exact behavior.** Result
routing (`fetchIntermediateStorages`, by ProcessId) is unchanged — group identity is only needed
command-side; all groups feed one writer.

*As implemented:* `IndexerClusterPlan.{h,cpp}` — `IndexerClusterEntry` + pure
`allocateIndexerSubprocesses()` (largest-remainder proportional, min 1 per non-empty cluster,
clamped to command count; unit-tested in `IndexerClusterPlanTestSuite`). `Project::buildIndex`
collects `(groupId, language, provider->size())` for C/C++ groups and passes the allocated plan
to `TaskBuildIndex` only when ≥ 2 clusters have commands; the ctor derives `m_processCount` and
the `ProcessId → groupId` pins from it. **Plus one necessary addition the sketch missed:** the
queue-fill task previously kept only `maximumQueueSize` (2!) commands in the SHM queue in
file-size order — pinned consumers would starve behind another group's backlog. With ≥ 2 source
groups, `TaskFillIndexerCommandsQueue` now fills **per group** (up to the same cap per group,
via `CombinedIndexerCommandProvider::consumeCommandForSourceGroup` +
`indexerCommandCountsBySourceGroup()` on the SHM manager); this also un-clogs the Rust/Swift
supervisors from a C++-heavy queue. Single group ⇒ legacy fill, byte-for-byte. Gate test:
`IpcIntegrationTestSuite` "group-aware queue fill" (per-group top-up, restock of a drained
group while the other group's commands sit queued, full drain exactly-once). Test-infra gotchas
recorded there: `IpcSharedMemory` truncates segment names to 18 chars, and two CREATE_AND_DELETE
owners of one segment in one process dangle the first owner's mutex view.

### S4 — Storage cutover: concurrent Turso as sole writer — **DONE (2026-07-14)**

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

*As implemented:* `TursoSqliteExport.{h,cpp}` streams every table (12, incl. `filecontent` and
`element_component`) in 500-row chunks through a new `ConcurrentTursoWriter::query()` row API
into typed sqlpp23 multi-row inserts on the shared `StorageConnection`, one transaction,
ids verbatim; the `.concurrent.turso*` scratch files are deleted after the drain. Sole-writer
mode is `PersistentStorage::setupConcurrentTursoSoleWriter()` (eager writer creation on the
main task thread, seeded `MAX(element.id)+1`), consulted by `TaskInjectStorage` (skips the
serial inject). **Gating is narrower than sketched: fan-out sole-writer requires a FULL
refresh** — the writer's in-process dedup index cannot see rows already present in a
copied-over incremental database, so incremental runs stay on the serial path (id-seeding
alone does not solve content dedup; noted for a future stage). `failedBatches > 0` currently
logs a hard error (S5 adds the degraded-run fallback). Gate tests: export round-trip
(`ConcurrentTursoWriterTestSuite`, counts + verbatim ids incl. seed offset) and
`scripts/smoke-fanout.sh` — generated two-group fixture vs single-group serial baseline,
per-table counts equal across all 12 tables, fan-out log markers asserted, no leftover
ingest files. Landing S4 also flushed out two pre-existing bugs (fixed alongside): empty-
C++-source-group commands lost their input file in `CxxParser::buildIndex` (every TU errored
"no input files" since the Cdb/Empty overload merge), and the IPC garbage-collector singleton
segfaulted at exit when destroyed during static teardown after libipc's handle cache.

### S5 — Gating / fallback — **DONE (2026-07-14)**

Auto-enable fan-out when `SOURCETRAIL_TURSO_CONCURRENT` is built **and** ≥2 enabled non-custom
groups; else today's exact path. Add a tri-state `ApplicationSettings` override (auto/on/off,
default auto). On `failedBatches() > 0` after retries: log a hard error and either re-run a serial
SQLite inject from the retained `m_storageProvider`, or (simpler first landing) mark the run
degraded via the `TaskFinishParsing` health warning. Interrupt/crash reuses the existing
200-consecutive-failure supervisor per ProcessId; a shared `m_stopSource` stops all clusters (an
incomplete DB is useless anyway).

*As implemented:* `"indexing/multi_group_fan_out"` in `ApplicationSettings` (JSON runtime
settings; no GUI — a power-user knob): `"auto"` (default) = clusters when ≥2 non-empty C++
groups, sole writer on full refreshes when clusters are active; `"on"` = clusters as in auto
**plus** sole writer on single-group full refreshes (lets the concurrent ingest be measured
without a second group); `"off"` = exact legacy path — no pins, no sole writer (the group-aware
queue fill stays: it is a Rust/Swift-supervisor starvation fix independent of fan-out).
Degraded runs take the simpler first landing: `PersistentStorage` records lost batches /
export failure, `TaskFinishParsing` emits the health warning + an error-status message telling
the user to re-run with `"off"`; the automatic serial re-inject from a retained
`m_storageProvider` remains future work. Verified: `"off"` on the two-group smoke fixture
produces zero cluster/sole-writer log markers and the identical graph via the legacy path.

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
