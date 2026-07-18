# Design: Storage-Layer Groundwork — Transport Codegen + Facet Model

**Status: Decided, not yet implemented. Sequenced *before* the remaining
declaration-modifier features (`open`/`final`, `@available`).** This note records
the storage-layer refactor that the fork constraint was hiding. As of the repo
isolation (upstream `petermost/Sourcetrail` remote removed, `trunk()` repointed
to `main@origin`, pushed `main` made immutable), there is **no upstream-sync
obligation and no backwards-compatibility constraint** — so the storage core can
be reshaped for its own sake, and the only remaining cost is intrinsic risk,
which we manage by sequencing.

See [DESIGN_NODE_MODIFIERS.md](DESIGN_NODE_MODIFIERS.md) for the three-axis model
this enables; this note is about *how the storage layer should carry it cleanly*.

## Why this is groundwork, not a feature

Two problems are orthogonal to any single feature and get worse with every new
facet and every new language:

1. **The tri-language transport tax.** Every intermediate-storage table exists
   four times: the `.fbs` schema (source of truth), and hand-written *owned
   mirror* types + serializers in C++, Rust, and Swift. There are **~10 tables**,
   so ~10 mirrors × 3 languages, each with read (`from_fbs`/`init(from:)`), write
   (build), and chunker cost accounting. Adding one field is four edits plus
   round-trip tests; the `modifiers` field touched **23 files**, eight of them
   Rust `OwnedStorageNode { … modifiers: 0 }` sites that carry nothing. A fourth
   language (Zig, mooted) multiplies the per-facet cost again.

2. **Heterogeneous facet storage.** Node facets live in three unrelated
   mechanisms — visibility in a side table (`component_access`), capabilities in
   an inline column (`node.modifiers`), and (proposed) config-guards in another
   side table. Two of the three are *shape-appropriate*; one (`component_access`
   as a side table rather than an inline column) is a historical accident the
   fork was propping up.

Neither is a bug. Both are **friction that compounds**. Fixing them first makes
the modifier features land cheaply instead of paying the tax again.

## Decision 1 — The facet-storage contract

A node carries facets in exactly **three** physical shapes, chosen by the facet's
*shape*, and this is the whole vocabulary — nothing else gets a bespoke table:

| Facet shape | Storage | Examples |
|---|---|---|
| **Fixed enum / boolean set** | inline column on the node row | `type` (NodeKind), `access` (AccessKind), `modifiers` (NodeModifier bitmask) |
| **Open-ended / sparse / display-only** | one generic `node_attribute(node_id, key, value)` table | `@available`, `cfg`, deprecation message, doc brief, symbol sub-kind |
| **Navigable relation** | edges | config atoms, macros, type arguments |

**The discipline:** *one bitmask column + one generic attribute table is the
entire sparse-facet vocabulary — never add another per-facet side table.* This is
the rule that prevents the N-table sprawl. `component_access` moves inline
(Decision 3), leaving `node_attribute` as the single side table for per-node
facts.

Rationale for not collapsing further: booleans in a key→value table are absurd;
availability strings in a bitmask are impossible; and edges are the only
navigable form. "Typed where you query, stringly where you only display, edges
where you navigate" is the clean split, not uniformity for its own sake.

## Decision 2 — Generate the transport mirrors from the schema

The `.fbs` is already the source of truth for the flatc reader/builder bindings.
Make it the source of truth for the **owned mirror types + pack/unpack** too, so
a field is one schema edit.

**Primary path — adopt flatc's object API.** FlatBuffers' `--gen-object-api`
generates native mutable "object" types (`StorageNodeT` + `Pack`/`UnPack`) that
are exactly the owned-mirror + serialize/deserialize we hand-wrote. The plan:

- Turn on `--gen-object-api` in the flatc invocations (`build.rs` for Rust, the
  CMake/`flatc --swift` step for Swift, the C++ generation step).
- Replace the hand-written `OwnedStorage*` (Rust `ipc/storage.rs`, Swift
  `IntermediateStorageTypes.swift`) and their serializers with the generated
  object types, keeping the *same field names* where the object API allows so the
  collection code (`collector.rs`, `StorageBuilder.swift`) changes minimally.
- **Per-language reality check is part of the work:** the C++ object API is
  mature; Rust's and Swift's flatc object-API coverage is thinner and must be
  validated against our needs (mutation during collection, optional-string
  ergonomics, vector-of-table building). Where a language's object API is
  inadequate, fall back to a **thin custom generator** driven off the same `.fbs`
  (a small script emitting the mirror + pack/unpack) rather than keeping the
  hand-written copies. Either way, the schema is the single source of truth.

**The one genuinely bespoke piece: chunker cost.** The self-contained-chunk
splitter (`StorageChunker.swift`, and the Rust equivalent in `ipc/storage.rs`)
needs a size estimate per element group. That is not transport, it is a
heuristic, and it should become a **thin derived helper** (fixed per-table
overhead + summed string lengths) computed from the generated types, not a wall
of hand-maintained `*Cost` constants that drift when fields are added.

**C++ is different on purpose.** The app's domain types (`StorageNode`,
`StorageEdge`, … in `src/lib/data/storage/type/`) are *not* transport mirrors —
they have behaviour (`operator<`, constructors) and are used throughout indexing,
the DB, and the graph. Codegen targets the **transport boundary only**
(`IntermediateStorageSerializer.cpp`, which bridges `.fbs` ↔ domain types); the
domain types stay hand-written. Do not conflate the two.

## Decision 3 — Consequences for the facet axes

Once Decision 2 lands, the facet work is cheap and rides on top:

1. **`node_attribute` table** — the sparse substrate (Decision 1). New `.fbs`
   table → generated mirrors everywhere for free; new SQLite table mirroring
   `component_access`; PersistentStorage inject; a consumer (node tooltip +
   filter). Emit `@available`/`cfg`/deprecation into it.
2. **`open`/`final` bits** on the existing `NodeModifier` bitmask — trivial.
3. **`access` inline / retire `component_access`** — move visibility from its
   side table to a `node.access` column so all fixed facets are inline and
   consistent. Pure structural gain; do it while the storage layer is open.
   (Free now that back-compat is a non-issue — a version bump + re-index.)

## Staged sequence (plan of record)

Each step is independently valuable, independently verifiable, and behaviour-
preserving except where noted:

1. **Transport codegen (Decision 2).** No schema change, no behaviour change.
   *Verification:* the generated serializers must round-trip byte-identically
   against the current hand-written ones on a captured corpus, plus the existing
   Rust/Swift/C++ round-trip tests stay green. This is the risky-but-mechanical
   step; it earns everything after it.
2. **`node_attribute` table** (Decision 1 + Decision 3.1). Storage version bump +
   re-index.
3. **`open`/`final`** (Decision 3.2). Two bits; same shape as `async`.
4. **`access` inline** (Decision 3.3). Optional consistency pass; version bump +
   re-index; delete `component_access`.
5. **Config-atom nodes + guard edges** — only if navigable "configuration
   surface" queries are wanted; additive over step 2.

Do **not** big-bang this. Step 1 touches the load-bearing storage core for zero
functional gain — its whole value is making 2–5 cheap — so it must land alone,
proven equivalent, before anything rides on it.

## Non-goals

- **No generic ORM / attribute-everywhere.** The `node_attribute` table is for
  *sparse, display-only* facts; anything queried on the hot path stays a typed
  column or an edge.
- **No C++ domain-type codegen.** Only the transport boundary is generated.
- **No migration tooling.** Back-compat is a non-issue; schema changes are a
  fresh re-index (the `isIncompatible()` → `PROJECT_STATE_OUTVERSIONED` path).

## Open questions

- Does flatc's Rust/Swift object API cover in-place mutation during collection and
  vector-of-table building well enough to delete the hand-written mirrors, or is
  a thin custom generator the lower-friction path? (Resolve in step 1 with a
  spike on the `StorageNode`/`StorageEdge` tables before committing.)
- Should the chunker cost helper live in generated code or a hand-written
  companion keyed by table name? (Prefer generated: it cannot drift.)

## Critical files

- `src/lib/data/indexer/interprocess/schemas/intermediate_storage.fbs` — source
  of truth for all transport types.
- `src/rust_indexer/indexer/build.rs`, the CMake `flatc --swift` step — where
  `--gen-object-api` gets enabled.
- `src/rust_indexer/indexer/src/ipc/storage.rs` — Rust owned mirrors + serialize +
  chunker (codegen target).
- `src/swift_indexer/.../IntermediateStorageTypes.swift`,
  `SwiftIndexerStorageChannel.swift`, `StorageChunker.swift` — Swift owned mirrors +
  serialize + chunker (codegen target).
- `src/lib/data/indexer/interprocess/serialization/IntermediateStorageSerializer.cpp`
  — C++ transport boundary (`.fbs` ↔ domain types); the only C++ codegen target.
- `src/lib/data/storage/type/Storage*.h` — C++ domain types (stay hand-written).
- `src/lib/data/storage/sqlite/{IndexTables.h,SqliteIndexStorage.cpp}` — DB schema
  (where `node_attribute` and inline `access` land).
- `src/lib/data/parser/{AccessKind,NodeModifier}.h` — the inline-facet enums/bits.
