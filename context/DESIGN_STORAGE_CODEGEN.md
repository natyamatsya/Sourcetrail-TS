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
   *Verification:* the C++ app must deserialize an object-API-packed buffer to the
   same `StorageX` values as today on a captured corpus — **semantic** round-trip,
   not byte-identity (flatbuffers does not guarantee identical bytes across builder
   paths, and the vtable reader does not need them) — plus the existing
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

## Spike findings (2026-07-18) — resolved: adopt the object API

Ran `flatc 25.12.19 --gen-object-api` on `intermediate_storage.fbs` for Rust and
Swift and inspected the output. **The object API covers both languages; no custom
generator is needed.** The hand-written `Owned*` mirrors + serializers can be
deleted in favour of the generated object types.

- **Rust — near-drop-in.** Generates `IntermediateStorageT { next_id, nodes:
  Option<Vec<StorageNodeT>>, … }` and per-table `StorageNodeT { id, type_,
  serialized_name: Option<String>, modifiers }` with whole-tree `pack()`/`unpack()`,
  deriving `Debug, Clone, PartialEq, Default`. `Default` alone kills the eight
  `OwnedStorageNode { … modifiers: 0 }` sites. The only friction is `Option`-wrapped
  strings/vectors (ours are unwrapped), a mechanical adaptation in `collector.rs`.
- **Swift — works, more friction.** Full object API (`Sourcetrail_Ipc_StorageNodeT:
  NativeObject` with `pack`/`unpack`/`serialize()`), but the `*T` types are
  **reference classes** and the container is `[StorageNodeT?]` (array of
  optionals), where our `OwnedStorage*` are value structs in `[T]`. So
  `StorageBuilder` (which mutates nodes in place, e.g. `nodes[i].modifiers |= x`)
  and `StorageChunker` need a moderate rework to reference semantics + optional
  elements. Doable, not free.
- **C++ — largely unaffected.** The app uses hand-written *domain* types
  (`StorageNode`, …) with behaviour, not owned transport mirrors; the single
  `IntermediateStorageSerializer.cpp` bridges `.fbs` ↔ domain. The object API adds
  little there (one file, not per-language mirrors), so C++ keeps its
  hand-written serializer.

**Chunker cost** stays a hand-written companion keyed by table, operating on the
generated `*T` types — but it should be *derived* (fixed overhead + string
lengths) so it cannot drift; it is a heuristic, not transport.

**Correction to the verification bar (below):** the goal is **semantic**
round-trip, not byte-identical. FlatBuffers does not guarantee identical bytes
across builder code paths, and the reader is vtable-based so it does not need
them — the bar is "the C++ app deserializes an object-API-packed buffer to the
same `StorageX` values," checked on a captured corpus plus the existing
round-trip tests.

### Rust cutover executed (2026-07-18) — 110 tests green

Ran the full Rust transport cutover (`--gen-object-api` is all-or-nothing per
schema, so "one table" is really the whole Rust storage layer — still one
language). Outcome: **it works, and it deletes the hand-written mirror.**

- `build.rs`: added `--gen-object-api`. Generated `*T` object types now carry the
  transport; `lib.rs` needed `extern crate alloc;` (flatc emits `alloc::` paths).
- The nine `OwnedStorage*` row structs became **type aliases** to the generated
  `*T` (`pub type OwnedStorageNode = StorageNodeT;` …) — collector/tests keep the
  `Owned*` names. A thin hand-written `OwnedIntermediateStorage` **container**
  (`Vec<*T>`) is kept for collection ergonomics + the chunker; it carries no
  per-field serialize logic, so the tax is still gone (a new `.fbs` field flows
  through with zero container changes).
- `serialize_one_storage` (~165 lines of per-field offset building) → build an
  `IntermediateStorageT` + `IntermediateStorageQueueT` and `.pack()` (~20 lines).
  `from_fbs` (~165 lines) → `.unpack()` + move (~15 lines). The test's hand-rolled
  `roundtrip()` (~150 lines replicating the serializer) collapsed to ~15.
- Ergonomic frictions, all minor: string fields are `Option<String>` in the
  object API (wrap collection sites in `Some(...)`, and a `decode_name(&Option…)`
  test helper); the chunker's four string cost helpers Option-adapt; `serialize`
  currently clones the row vectors to move them into the object type (a by-value
  `push` signature removes the clone — deferred).

Verified: `cargo test` — **110 passed, 0 failed**, including the full
serialize→bytes→deserialize round-trip and the collector/chunker suites. The wire
format is unchanged (same schema, vtable reader), so the C++ app reads the packed
buffer as before.

### Swift cutover executed (2026-07-18) — 40 tests green

Ran the Swift transport cutover too. Same shape as Rust, with the reference-type
frictions the spike predicted — all resolved:

- CMake `flatc --swift` step gained `--gen-object-api`.
- The nine `OwnedStorage*` row types became **typealiases** to the generated
  `Sourcetrail_Ipc_*T` classes. Because the `*T` classes have no memberwise init,
  `IntermediateStorageTypes.swift` adds **convenience inits** matching the old
  value-struct signatures — so `StorageBuilder` and the tests construct rows
  unchanged. A hand-written `OwnedIntermediateStorage` container (`[*T]`) is kept.
- Reference semantics actually **simplified** `StorageBuilder`: the
  replace-to-upgrade dances (`nodes[i] = OwnedStorageNode(…preserve modifiers…)`)
  became in-place `nodes[i].type = kind`.
- `serializeStorage` (~120 lines of per-field offset building) → map the
  container arrays into an `IntermediateStorageT` (whose vectors are `[T?]`,
  hence `.map { Optional($0) }`) and `pack` (~20 lines).
- Frictions, all minor: string fields are `String?` (chunker cost helpers and a
  handful of test reads adapt with `?? ""`; the `decodeParts` test helper widened
  to `String?`).

Verified: `swift test` — **40 passed, 0 failed**, including the serialize
round-trip (`StorageChannelTests`) and the chunker suite. Wire format unchanged.

**Codegen step 1 is now complete in both indexers.** Remaining polish (optional,
not blocking): the Rust `serialize` by-value optimization to drop the row-vec
clone; and eventually retiring the hand-written containers for the generated
`IntermediateStorageT`/`IntermediateStorageQueueT` directly (accepting the
optional-element vectors at the collection sites). The C++ transport boundary
(`IntermediateStorageSerializer.cpp`) stays hand-written by design.

## Step 2 executed (2026-07-18) — `node_attribute` substrate landed

Built the sparse side-table substrate (Decision 1 + 3.1) end-to-end. It is
**behaviour-preserving** — the table exists and round-trips through every layer,
but nothing emits into it yet, so a fresh index just carries an empty table. That
is the point: the load-bearing plumbing lands proven-equivalent before any
feature rides on it.

- **Schema:** `StorageNodeAttribute { node_id:int64; key:int32; value:string }`
  + `node_attributes:[…]` on `IntermediateStorage`. `key` is a `NodeAttributeKind`
    enum (append-only: NONE/AVAILABILITY/DEPRECATED/CFG/DOC_BRIEF); `value` is the
    display-only payload. The transport mirrors flowed through **for free** in
    Rust and Swift (the object-API cutover from step 1) — one container field, one
    chunker pass, one cost helper each; no hand-written serializer.
- **C++ transport** (`IntermediateStorageSerializer.cpp`) is hand-written by
  design, so it got the two directions explicitly, plus a new domain type
  `StorageNodeAttribute` (ordered by the full `(nodeId, key, value)` triple —
  unlike `StorageComponentAccess`, a node may carry several) and the
  `Storage`/`IntermediateStorage` add/get/set surface.
- **Persistence:** new `node_attribute` SQLite table (typed sqlpp23 insert;
  hand-written DDL as the schema source of truth in `index.sql` + `setup()`); the
  concurrent **Turso** writer path (`ConcurrentTursoWriter` + `ConcurrentStorageIndex`
  dedup); `Storage::inject` remap; the C++ `IntermediateStorageChunker`. Storage
  version **26 → 27** (fresh re-index; no migration, per the non-goals).
- **The discipline held:** this is the *single* generic side table. No new
  per-facet table — `open`/`final` (step 3) go on the `NodeModifier` bitmask,
  `access` (step 4) moves inline. `node_attribute` never grows a sibling.

Verified: Rust `cargo test` **111 passed**; Swift transport/chunker/access suites
**12 passed** (incl. a node-attribute serialize round-trip and the chunker's
self-containment check now carrying attributes); C++ `Sourcetrail_test` serializer
round-trip + a new `SqliteIndexStorage` node-attribute persistence round-trip.

**First producer + consumer landed (2026-07-18).** Swift `@available` extraction
→ `NodeAttributeKind.AVAILABILITY` (both the semantic and syntactic engines, via
the shared `swiftAvailability` extractor keyed like SW16 access), and the C++ node
tooltip appends `@available(...)` (`PersistentStorage::getTooltipInfoForTokenIds`
reads `getNodeAttributesByNodeIds`). Tested end-to-end: Swift producer (syntactic +
semantic) + a C++ `StorageTestSuite` tooltip round-trip. This is the deferred
`@available` point from SW16, now riding the substrate.

**Deprecation landed (Swift, 2026-07-18)** as the cross-axis fact: a
`NODE_MODIFIER_DEPRECATED` bit (→ "deprecated" in the graph node label via
`nodeModifierToString`) plus a `DEPRECATED` `node_attribute` for the message,
extracted from `@available(*, deprecated[, message:])`; the tooltip shows
`[deprecated: …]`.

**Graph treatment for deprecation landed (2026-07-18):** deprecated nodes get an
orange dashed warning outline (`QtGraphNodeData` off `Node::isDeprecated()`), and a
"hide deprecated" graph filter (`ApplicationSettings::hide_deprecated_in_graph` +
`GraphController::hideDeprecated` + a preferences checkbox, modelled on the
built-in-types filter) drops them from every graph view (the active node stays).
Both key off the `NODE_MODIFIER_DEPRECATED` bit, not the message table.

**Rust producers landed (2026-07-18):** `collector.rs` `scan_item_attrs` emits
`#[deprecated]` (bit + `DEPRECATED` message) and `#[cfg(...)]` (→ `CFG` predicate);
the tooltip gained a `#[cfg(<pred>)]` line.

**C++ producer landed (2026-07-18):** the clang pipeline gained a producer seam —
`recordNodeModifier` / `recordNodeAttribute` on `ParserClient`/`ParserClientImpl`
(backed by `IntermediateStorage::addNodeModifier`) — and
`CxxAstVisitorComponentIndexer::recordDeprecation` reads `[[deprecated]]` at each
named-decl site (bit + `DEPRECATED` message). Deprecation is now cross-language
(Swift/Rust/C++).

**Remaining follow-ups (additive over the same table):** Clang `availability` →
`AVAILABILITY`, C++ `#ifdef`/`#if` → `CFG`, `DOC_BRIEF`, and the `open`/`final`
Axis-2 bits. The canonical open-items checklist lives in
`context/DESIGN_NODE_MODIFIERS.md` ("Status & remaining work").

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
