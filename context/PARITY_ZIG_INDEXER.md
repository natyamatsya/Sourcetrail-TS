# Zig Indexer — Parity Report vs. Rust & Swift

Where the experimental Zig indexer stands against the two mature out-of-process
language backends (Rust = rust-analyzer-embedded, `src/rust_indexer/`; Swift =
IndexStoreDB-backed, `src/swift_indexer/`). All three are standalone binaries
talking to the C++ core purely over thoth-ipc shared memory + FlatBuffers.

**Bottom line.** The Zig indexer is at **full parity on the IPC contract and
transport** (channels, framing, back-pressure, command round-trip) and — as of
this pass — on **storage chunking** (the 16 MiB per-file cap is gone). It
indexes real code the other two can't reach in scale terms (the Zig compiler:
168 files / 517k LOC, incl. a 190k-line file, in 88 s, 0 crashes). The gaps are
all on the **semantic-richness** axis: it emits fewer node/edge kinds and omits
the display-only side-tables (local symbols, node attributes, component access,
modifiers) and the proper `NameHierarchy` wire format. None of those block
indexing; they reduce how much the GUI can show.

Legend: ✅ at parity · 🟡 partial / divergent · ❌ not implemented · — n/a for Zig.

---

## 1. Transport & IPC contract — ✅ parity

| Aspect | Rust | Swift | Zig | Status |
|---|---|---|---|---|
| Command SHM (`icmd_ipc_<uuid>`) | 64 MiB | 64 MiB | 64 MiB | ✅ |
| Storage SHM (`iist_ipc_<pid>_<uuid>`) | 16 MiB | 16 MiB | 16 MiB | ✅ |
| Status SHM (`ists_ipc_<uuid>`) | 1 MiB | 1 MiB | 1 MiB | ✅ |
| Queue framing `[u64 cap][u32 count][u32 size][bytes]…` | ✅ | ✅ | ✅ (`ipc/queue.zig`) | ✅ |
| `needed_capacity` written as 0 (no segment growth) | ✅ | 🟡 grow-signal | ✅ | ✅ |
| Named mutex, RMW under one lock | ✅ | ✅ | ✅ (`ipc/shm.zig`) | ✅ |
| "empty" = first 4 bytes zero | ✅ | ✅ | ✅ | ✅ |
| Command pop preserves other-language commands verbatim | re-serialize all fields | re-serialize all fields | flatcc `_clone` of every other command | ✅ |
| Main loop: interrupt → pop → index → push → finish | ✅ | ✅ | ✅ (`main.zig runIpc`) | ✅ |

The Zig command channel deep-clones every non-Zig command with flatcc's
generated `_clone` when rewriting the queue, so it structurally preserves *all*
fields of Cxx/Rust/Swift commands (no per-field mirror to keep in sync — a small
robustness edge over the hand-maintained `OwnedIndexerCommand` mirrors).

One divergence, benign: back-pressure polls every **2 ms** (`nanosleep`) vs. the
Rust/Swift **200 ms**. Same `count() < 2` in-flight limit; Zig just reacts
tighter (slightly more spins, negligible for the single-worker case).

---

## 2. Storage chunking — ✅ parity (this pass)

**The flagged gap, now closed.** Previously the Zig storage channel serialized a
whole file's `IntermediateStorage` as one queue entry and, above 16 MiB, failed
with `SegmentTooSmall` — swallowed by `indexOneCommand`'s `catch {}`, silently
dropping that file's entire graph.

`src/chunker.zig` now ports the Rust/Swift design exactly:

| Aspect | Rust | Swift | Zig |
|---|---|---|---|
| Chunk byte budget | 7 MiB | 7 MiB | **7 MiB** |
| Fast path when store ≤ budget | single chunk (clone) | single chunk | single chunk (borrows rows, no copy) |
| Split unit | element groups | element groups | element groups |
| Edge carries endpoint node stubs | ✅ | ✅ | ✅ |
| Occurrence carries its location (+ file node) | ✅ | ✅ | ✅ |
| Occurrence/symbol emitted exactly once | ✅ | ✅ | ✅ |
| Cost model (`edge 56`, `location 64`, `stub_max 512`, …) | ✅ | ✅ | ✅ (same constants) |
| `next_id` copied onto every chunk | ✅ | ✅ | ✅ |

Chunks are self-contained because the app dedups nodes/edges/files/locations by
content on inject but **not** occurrences/symbols. Unit tests assert both the
single-chunk fast path and that an oversized store (1200 large nodes) splits into
≥2 chunks in which every edge endpoint and occurrence location is present
in-chunk, with each occurrence/edge emitted once.

In practice Zig indexes one file per command, so single files rarely approach
7 MiB (the compiler's largest, a 190k-line file, produced ~15k locations ≈ a few
MiB). The chunker is the safety net that makes a pathological generated file
correct instead of silently dropped.

---

## 3. Status channel — 🟡 mostly parity

| Aspect | Rust/Swift | Zig | Status |
|---|---|---|---|
| interrupt flag read | ✅ | ✅ | ✅ |
| `indexing_file_paths` / progress | ✅ | ✅ | ✅ |
| `current_files` per-pid RMW (cleared on finish) | ✅ | ✅ | ✅ |
| `finished_process_ids` append | ✅ | ✅ | ✅ (hand-rolled u64-vec reader) |
| `start_indexing` crash bookkeeping → `crashed_file_paths` | ✅ | ❌ | 🟡 |
| verify (`getCheckedRoot`) on read, surface corruption | ✅ | 🟡 `as_root`, no verify | 🟡 |
| empty vectors omitted (null) on write | ✅ (ADR-0003) | ❌ writes empty vectors | 🟡 |

Two genuine divergences, both low-severity:

- **No explicit crashed-TU bookkeeping.** Rust/Swift's `start_indexing` moves a
  still-open `current_files` entry to `crashed_file_paths` when the same pid
  starts a new file without finishing the last (i.e. the previous file crashed).
  Zig's `updateIndexing` silently clears the stale entry instead. A file whose
  Zig worker crashes mid-index is thus not surfaced through `crashed_file_paths`
  (the C++ supervisor still detects leftover `current_files` on process exit, so
  it is not lost — just reported through a different path).
- **Writes empty FlatBuffers vectors** rather than omitting them. Rust/Swift emit
  `null` for empty vectors (ADR-0003), which also sidesteps the mis-aligned empty
  `[uint64]` that caused the earlier read crash. Zig instead survives it on the
  read side with a memcpy accessor shim + a hand-rolled u64-vector reader
  (`ipc/status.zig readU64VecField`). Adopting the omit-empty convention on write
  would be a cheap, principled follow-up.

---

## 4. Analysis frontend

| | Rust | Swift | Zig |
|---|---|---|---|
| Syntactic pass | `syn` | SwiftSyntax | `std.zig.Ast` (`parser.zig`) |
| Semantic engine | rust-analyzer (embedded) | IndexStoreDB (compiler index) | ZLS 0.16 `Analyser`/`DocumentStore` (`semantic.zig`) |
| Cross-file goto-def / find-refs | ✅ | ✅ (compiler-accurate) | ✅ (ZLS best-effort; degrades to syntactic) |
| Freshness gating | queue pop/rewrite | per-file unit-date vs mtime | file mtime → content diff (C++ side) |
| Result caching across commands | crate-root cache | — | — (per-file, no cache) |

Zig's semantic layer is the youngest of the three: ZLS resolution is best-effort
(comptime/type resolution is WIP upstream) and degrades to the syntactic result,
mirroring how the Rust indexer degrades on unexpanded macros.

---

## 5. Feature coverage matrix

### Node kinds emitted

| Kind | Rust | Swift | Zig |
|---|:--:|:--:|:--:|
| file, struct, function, method, field, enum, enum_constant, global_variable | ✅ | ✅ | ✅ |
| union | ✅ | ✅ | ✅ |
| module | ✅ | ✅ | ❌ (Zig files *are* modules) |
| typedef (type alias) | ✅ | ✅ | ✅ (ZLS `is_type_val`, non-import consts) |
| type_parameter (generics) | ✅ | ✅ | ✅ (`comptime T: type` params) |
| interface / class | trait | protocol/class | — (no OOP) |
| macro | ✅ | ✅ | ❌ (Zig has no macros) |
| symbol (fallback) | ✅ | ✅ | ✅ (reference targets) |

### Edge kinds emitted

| Kind | Rust | Swift | Zig |
|---|:--:|:--:|:--:|
| member, type_usage, usage, call | ✅ | ✅ | ✅ |
| include / import | import | import | ✅ include (file→file, for reverse-dep) |
| inheritance, override | ✅ | ✅ | — (no inheritance) |
| type_argument, template_specialization | ✅ | ✅ | ❌ (generics not modelled) |
| macro_usage, annotation_usage | ✅ | ✅ | ❌ |

### Side tables & metadata

| Feature | Rust | Swift | Zig |
|---|:--:|:--:|:--:|
| occurrences + token/scope locations | ✅ | ✅ | ✅ |
| definition kind (none/implicit/explicit) | 3 | 3 | 🟡 explicit only |
| local symbols (fn-local bindings) | ✅ | ✅ | ✅ (`file<line:col>` convention) |
| local-symbol locations (type 3, GUI highlight) | ✅ | ✅ | ✅ |
| errors (parse/resolve) as StorageError | ✅ | ✅ | ✅ (parse errors) |
| component access (public/private/…) | ✅ | ✅ | ✅ (`pub`→public, else default, type params→type_parameter) |
| node modifiers bitmask | 🟡 deprecated | ✅ actor/async/… | ❌ (always 0) |
| node attributes (deprecated/cfg/availability/doc) | ✅ | ✅ | ❌ |
| proper `NameHierarchy` wire format | ✅ | ✅ | ✅ (`[file, …parts]`, `.`/`/` delimiters) |

---

## 6. Gaps, ranked

| # | Gap | Severity | Effort | Notes |
|---|---|---|---|---|
| ~~1~~ | ~~`NameHierarchy` wire format~~ | — | — | **Done** (`f6e9529f33`): emits `[file, …parts]` with `.`/`/` delimiters; ZLS deserialize errors dropped from hundreds to 0. |
| ~~2~~ | ~~Local symbols (fn-local bindings + type-3 locations)~~ | — | — | **Done** (`de1673c97a`): ZLS-resolved locals recorded as `file<line:col>` rows + type-3 occurrences; 10.3k locals / 39.8k occurrences on ZLS. |
| ~~3~~ | ~~`typedef` and `type_parameter` node kinds~~ | — | — | **Done** (`ddece0e633`): `comptime T: type` params → NODE_TYPE_PARAMETER; `const T = <type>` upgraded to NODE_TYPEDEF via ZLS `is_type_val`. 75 typedefs / 31 type params on ZLS. |
| ~~4~~ | ~~Component access from `pub`~~ | — | — | **Done** (`f2e0b5e98f`): `pub`→public, else default, generic params→type_parameter. 811/3035/31 rows on ZLS (one per symbol node). |
| ~~5~~ | ~~Typedef references as EDGE_TYPE_USAGE~~ | — | — | **Done** (`b3e3b86996`): wireOne reads the target's actual (reclassified) node kind. Same-file typedef refs now TYPE_USAGE (486/500 on ZLS); cross-file refs still USAGE (a referencing file can't see another file's const is a typedef without a per-ref ZLS resolve). |
| 6 | Status: omit empty vectors on write; crashed-TU bookkeeping | Low | S | Align with ADR-0003 and the `start_indexing` crash path. |
| 7 | Node attributes (deprecated/doc) | Low | M | Display-only side table; least impactful. |
| 8 | `implicit` definition kind | Low | S | Zig has few compiler-synthesized decls to mark. |
| 9 | Node modifiers bitmask | Low | S | Zig has no deprecated/async modifiers to surface yet. |

None block indexing. With #1–#5 done the full index runs with **zero** logged
errors, full local-variable highlighting, typedef/generic modelling, type-usage
edges, and visibility markers. What remains is IPC-hardening polish (status
niceties #6) and two display side-tables Zig has little to populate (node
attributes #7, node modifiers #9).

---

## 7. Where Zig leads

- **Incremental precision.** `@import` resolves (via ZLS) to a real absolute path
  and emits `EDGE_INCLUDE` (file→file), giving Sourcetrail an exact reverse-
  dependency closure with no textual-include churn: edit a leaf → 1 file
  re-indexed; edit a dependency → exactly its importers. This is the study's
  original motivation and it works end-to-end.
- **Scale demonstrated.** Full index of the Zig compiler `src/` — 168 files,
  517k LOC, a single 190k-line file — in 88 s, 0 crashes, 0 parse errors. ZLS
  (59k LOC) in ~13 s.
- **Transport robustness.** flatcc-in-Zig unaligned-read hazard is handled
  centrally (accessor shim + hand-rolled u64-vector reader), and the command
  channel preserves other languages' commands via structural `_clone` rather than
  a hand-maintained field mirror.

---

## 8. Verdict

The Zig indexer is a **production-shaped peer** on everything that governs
correctness and throughput — IPC contract, chunked storage, back-pressure,
incremental reverse-deps, the `NameHierarchy` wire format (a full index runs
with zero logged errors), local-variable highlighting, typedef/generic
modelling, type-usage edges, and visibility markers. Its remaining distance from
Rust/Swift is a small amount of IPC-hardening polish (status niceties #6) and
two display side-tables Zig has little to fill (node attributes #7, modifiers
#9). Those are additive polish on a foundation that is at parity, not
structural rework.
