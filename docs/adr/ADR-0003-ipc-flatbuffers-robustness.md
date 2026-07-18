# ADR-0003: IPC FlatBuffers robustness — null empty vectors on write, surface verification failures on read

- **Status:** Accepted
- **Date:** 2026-07-18
- **Deciders:** natyamatsya
- **Related:** IPC status/storage channels
  (`src/lib/data/indexer/interprocess/serialization/{IndexingStatusSerializer,IntermediateStorageSerializer}.cpp`,
  `src/swift_indexer/.../SwiftIndexerStatusChannel.swift`,
  `src/rust_indexer/indexer/src/ipc/status.rs`), ADR-0001 (errors-as-values),
  ADR-0002 (fixed-size SHM). Fix commits `4361b290`, `e6f7499c`.

## Context

The app (C++) and the indexer subprocesses (Rust, Swift) exchange FlatBuffers
messages over shared memory: `IndexerCommand` (app → indexer), `IndexingStatus`
(app ↔ indexer — carries the `indexing_interrupted` / `queue_stopped` control
flags), and `IntermediateStorage` (indexer → app — bulk results). Each schema is
written and read by at least two independent FlatBuffers implementations
(flatc-generated C++, the thoth-ipc Rust and Swift bindings), which do **not**
agree on every wire detail.

A silent, months-long bug exposed two independent weaknesses:

1. **Empty scalar vectors are aligned inconsistently across implementations.** The
   C++ writer emitted an *empty* `finished_process_ids: [uint64]` via
   `CreateVector`. An empty 8-byte-element vector is only 4-aligned in the C++
   builder; the strict Swift verifier (`getCheckedRoot`) demands 8-byte alignment
   for a `uint64` vector and rejected the whole buffer as `missAlignedPointer`.
2. **The reader silently substituted a default on that failure.** Swift's
   `decodeStatus` caught the verification error and returned an empty
   `IndexingStatus`. So every app → indexer flag was dropped: a user **interrupt
   never reached the Swift indexer**, invisibly. (The Rust reader had the same
   shape — `root_as_indexing_status(...).unwrap_or(false)`.)

Either weakness alone would have been survivable; together they produced a
correctness bug with no diagnostic whatsoever.

## Decision

Two rules govern every IPC FlatBuffers message.

### 1. Writers omit empty vectors (never emit an empty FlatBuffers vector)

Every vector field is written as a **null offset when empty**, not as a
zero-length vector. A null field reads back as an empty vector in every binding,
so this is purely a wire-form choice with no semantic cost.

- **Strictly required** for scalar vectors whose element is ≥ 8 bytes
  (`[uint64]`, `[int64]`, `[double]`, …): the empty-vector alignment discrepancy
  above only bites those. Offset vectors (vectors of tables/strings) are 4-aligned
  and verify either way.
- **Applied to *all* vectors** regardless, so there is one simple rule, an
  identical empty-message wire form across the three frontends, and no latent
  landmine if a scalar-vector field is added later.

### 2. Readers distinguish "empty segment" from "malformed", and surface the latter

- A zeroed / sub-header-size segment is a **legitimate empty message** — the peer
  simply has not written one yet. Return the empty/default value; this is not an
  error.
- A **non-empty buffer that fails verification is a fault.** Do **not** silently
  substitute a default — that hides the fault and can drop control data. Surface
  it as an error value (Rust `io::Result::Err`, Swift `throws`) and let the caller
  decide; the caller must at minimum **log** it (never `try?` / `unwrap_or` it
  into oblivion).

## Compliance

| Message / role | Writer: null empties | Reader: verify + surface |
|---|---|---|
| `IndexingStatus` — C++ (app) | ✅ `IndexingStatusSerializer` | ✅ `deserializeIndexingStatus` verifies (`VerifyIndexingStatusBuffer`); on failure logs and degrades to empty |
| `IndexingStatus` — Rust (indexer) | ✅ `status.rs` `to_bytes` | ✅ `is_interrupted` returns `Err`; `main.rs` logs it |
| `IndexingStatus` — Swift (indexer) | ✅ `serializeStatus` | ✅ `decodeStatus` throws `invalidIndexingStatus`; main loop logs it |
| `IntermediateStorage` — indexers (write only) | Rust/Swift: offset vectors only (no scalar-vector fields), so unaffected today; the null-empties rule still applies to any future scalar vector | app-side reader only |
| `IntermediateStorage` — C++ (app, read) | — | unchecked `GetRoot` — see note |

**Note on the app-side C++ readers.** The small control-message reader,
`deserializeIndexingStatus`, **verifies** (`VerifyIndexingStatusBuffer`) and on
failure logs and degrades to an empty status — it has no error-return channel
(`IndexingStatusData` by value), so the log *is* the surfacing, and returning
empty is the degraded value. The bulk-data reader,
`deserializeIntermediateStorage`, stays on the unchecked `GetRoot` **by design**:
verifying every multi-megabyte chunk (ADR-0002) on the hot inject path is not
worth it for a trusted producer that rule 1 already keeps well-formed. So the
control channel (`IndexingStatus`) is verified end-to-end in all three frontends;
the bulk channel (`IntermediateStorage`) relies on the writer contract plus the
trusted-consumer boundary.

## Consequences

**Positive**
- Control flags (`indexing_interrupted`, `queue_stopped`) are delivered reliably
  across all three frontends; the interrupt path has a regression test
  ("swift indexer honors the interrupt flag set by the app").
- Malformed control messages are visible in logs, never silently swallowed.
- One empty-message wire form across C++/Rust/Swift; no empty-scalar-vector
  alignment landmine.

**Negative / costs**
- Writers carry a per-field `empty ? null : createVector` check.
- Readers must special-case the empty/uninitialized segment so it is not mistaken
  for a fault.

**Neutral**
- The rule is vacuous for messages that currently have no scalar-vector fields
  (`IndexerCommand`, `IntermediateStorage`), but is the standing convention for
  new fields and new schemas.

## References
- `docs/adr/ADR-0001-expected-error-channel.md` — errors-as-values.
- `docs/adr/ADR-0002-no-shm-growth.md` — fixed-size SHM / chunked storage (why the
  storage reader stays unchecked).
- Commits `4361b290` (writer omits empty `[uint64]`, Swift verified read restored),
  `e6f7499c` (null all empty status vectors; Swift surfaces decode errors), and the
  Rust `is_interrupted` / Swift `serializeStatus` alignment that accompanies this ADR.
