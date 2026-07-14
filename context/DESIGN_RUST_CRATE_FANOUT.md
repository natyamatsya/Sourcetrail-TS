# Design: Crate-Level Fan-Out for the Rust Indexer (Phase 8 follow-up)

**Status: designed, not implemented (2026-07-14).** The Rust analog of
[DESIGN_MULTIGROUP_FANOUT.md](DESIGN_MULTIGROUP_FANOUT.md) (S0–S5, complete): run **K Rust
supervisor subprocesses draining the same crate-command queue** instead of one. Named there as
"allow K Rust supervisors is a later extension" (S3).

Related: [ROADMAP_RUST_INDEXER.md](ROADMAP_RUST_INDEXER.md) (the indexer itself),
[DESIGN_TURSO_BACKEND.md](DESIGN_TURSO_BACKEND.md) (the concurrent writer, if ingest ever
becomes the bottleneck).

## The unit of fan-out is the crate, not the source group

C++ fan-out had to *create* its parallelism structure: file-level commands pool per source
group, so commands were tagged (S1), pops filtered (S2), and subprocesses pinned per group (S3).
Rust already has the right granularity for free. A single `Rust Empty` source group over a
workspace produces **one indexer command per crate** (the fill task's dedup by working
directory), and each command is a self-contained work unit: the supervisor loads the workspace
at that crate root via `ra_ap_load_cargo::load_workspace_at` and analyzes the crate
(`src/rust_indexer/indexer/src/parser/mod.rs`, `index_crate_with`).

What limits throughput today is the **consumer side**: exactly one supervisor
(`TaskBuildIndex::runRustIndexerProcess`, ProcessId `processCount+1`) pops crate commands
sequentially.

Measured motivation (release build, 2026-07-14, M-series 12-core, serial writer):

| project | crates | parse wall | writer busy | stalls |
|---|---|---|---|---|
| ripgrep | 1 | 7.9 s | 0.23 s | 0 |
| tokio | 3 | 17.0 s | 2.6 s | 0 |
| rust-analyzer | 10 | 26.4 s | 2.7 s | 0 |

Ten crates in sequence; with K supervisors, wall trends toward
`max(crate time) + overhead` instead of the sum. Parse-bound throughout (writer stalls 0), so
the serial SQLite writer stays sufficient — same finding as the C++ phase.

## What is NOT needed (contrast with C++ S1/S2)

- **No group tags, no pop filters, no pinning.** The SHM queue pop is mutex-serialized;
  K supervisors doing accept-any pops naturally load-balance by each taking a different crate.
  Work-stealing — which the C++ pinning deliberately gave up — comes for free.
- **No storage cutover.** Results funnel per-ProcessId into the existing merge/inject stream;
  cross-crate symbol dedup happens in the serial writer's inject exactly as it does today for
  crates processed sequentially. The concurrent Turso sole writer (S4) stays one flag away if
  K supervisors ever push the writer past its ceiling.

## Design

### R1 — K supervisors

`TaskBuildIndex::doEnter`: spawn `rustSupervisorCount` supervisors instead of one, with
sequential ProcessIds after the C++ range (`processCount+1 .. processCount+K`; the Swift
supervisor id shifts accordingly — it is computed relative, `swiftIndexerProcessId`, so this is
mechanical). One `IntermediateStorageManagerImpl` channel per supervisor;
`fetchIntermediateStorages` is already per-ProcessId. The babysitter loop, interrupt path and
the 200-consecutive-failure guard are reused unchanged per supervisor.

### R2 — supervisor count: memory-aware, not proportional

Unlike C++ (thread budget split by command counts), the Rust ceiling is **memory**: every
supervisor loads its crate's workspace view independently (rust-analyzer-scale, potentially
GBs on large workspaces), and the FULL load profile already runs internal parallelism
(`num_worker_threads` up to 8 + 2 proc-macro processes per supervisor). So:

    rustSupervisorCount = clamp(1, min(crateCommandCount, indexerThreadCount / 4, hardCap))

with `hardCap` ~3–4 as the first landing (rationale: K supervisors × up to 8 loader threads
oversubscribes a 12-core machine quickly, and K× workspace memory is the real limit). Crate
count is known at fill time (post-dedup); simplest carrier is the same place the C++ cluster
plan is built in `Project::buildIndex` — count Rust-typed commands per working directory
before the provider moves.

Gate behind the existing `"indexing/multi_group_fan_out"` tri-state ("off" ⇒ one supervisor,
exactly today's path).

### R3 — cargo/target-dir contention (the real new work)

Each command may run `cargo check` (build scripts, dependency proc-macro dylibs — the
`out_dirs` load profile flag). Concurrent supervisors on one workspace share the target dir;
cargo's file lock serializes that phase, and worst case K supervisors redundantly build the
same dependency graph. Mitigations, in order of preference:

1. **Pre-warm**: run a single `cargo check` for the workspace before fan-out (a Rust
   `getPreIndexTask`, mirroring the C++ PCH pre-index task). After it, per-crate checks are
   cache hits and the lock is held only briefly.
2. Accept the serialization: the lock only covers the check phase; loading and analysis (the
   bulk) proceed concurrently.
3. Rejected: per-supervisor `CARGO_TARGET_DIR` — K× disk and K× dependency builds.

### R4 — accounting

The progress accounting assumes one command per source path entry: runs show
`"Finished indexing: 10/1 source files"` (crates indexed vs. post-dedup expected count).
Cosmetic today, but K supervisors make it more visible — fix the expected count to the
post-dedup crate count when the Rust package is active.

## Verification

- Extend `scripts/smoke-rust.sh` (or add `smoke-fanout-rust.sh`): index a generated
  multi-crate workspace with K=1 and K=3; per-table counts must match (same
  order-independent equivalence gate as `scripts/smoke-fanout.sh`).
- Real-project gate: tokio / rust-analyzer with K=1 vs K=3 — counts equal, wall time
  reported; the payoff bar is wall trending toward the largest crate.
- Watch `logIndexingSummary` stalls: if K supervisors start stalling the writer, that is the
  trigger for extending the S4 sole-writer gating to Rust runs.

## Risks

1. **Memory**: K × workspace load. Mitigated by the conservative `hardCap` and making K
   user-visible through the existing fan-out setting. Measure RSS in the smoke.
2. **cargo lock pathologies** (R3): pre-warm reduces it to a cold-start concern.
3. **Thread oversubscription**: loader threads × K; mitigated by the `indexerThreadCount / 4`
   term — and if needed, scale `num_worker_threads` down when the supervisor knows it has
   siblings (pass K via the existing flag-style CLI argument mechanism from S2).
4. **Skew**: one giant crate dominates (same as one giant group in C++); fan-out still wins
   on everything else finishing in parallel.
