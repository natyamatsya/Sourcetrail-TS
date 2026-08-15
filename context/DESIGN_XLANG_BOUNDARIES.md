# Design: Cross-language boundaries — indexing and visualization

**Status: Proposed, not implemented.** Groundwork (X0) is independently useful and
should land alone. Nothing here requires the analysis engines of
[ROADMAP_ANALYSIS_ENGINES.md](ROADMAP_ANALYSIS_ENGINES.md); the boundary is
recorded by producers, not derived.

Related: [DESIGN_STORAGE_CODEGEN.md](DESIGN_STORAGE_CODEGEN.md) (the facet
contract this obeys — no new side tables),
[DESIGN_MULTIGROUP_FANOUT.md](DESIGN_MULTIGROUP_FANOUT.md) (a project is already
multi-group and multi-language), [DESIGN_NODE_MODIFIERS.md](DESIGN_NODE_MODIFIERS.md)
(Axis 3b's config atoms — the shape this borrows),
[ROADMAP_ANALYSIS_ENGINES.md](ROADMAP_ANALYSIS_ENGINES.md) (where *derived* links
would live later).

## Why this is not a feature request

A Sourcetrail project has been multi-language since source groups existed, and
this fork indexes four languages into one database. The interesting questions in
such a project are almost always at the seams: *who calls this C function from
Rust; which Swift type is this FlatBuffers table; if I change this schema, what
breaks.* The graph cannot express any of them today, and that is not a gap in
coverage — it is three concrete defects:

1. **The boundary is dropped.** `SemanticIndexer.swift:120` filters every
   non-Swift symbol out of a mixed target (`guard symbol.language == .swift`),
   which is precisely the set of symbols that constitute a boundary. Zig
   classifies `@cImport` only to *suppress* it (`semantic.zig:354-364`). Rust
   ignores `extern "C"` and `#[no_mangle]`; `src/lib_cxx` has no language-linkage
   handling at all.
2. **The boundary is conflated.** Node identity is `serialized_name` and nothing
   else, at all three merge layers (`IntermediateStorage.inl:129-147`,
   `Storage.inl:28-42`, `SqliteIndexStorage.cpp:398-505`). Rust
   (`collector.rs:306-324`) and Swift (`StorageKinds.swift:82-96`) both emit
   `"::\tm"` — the C++ delimiter. A Rust `foo::bar` and a C++ `foo::bar` silently
   become **one node**, with `type` widened to the max of the two
   (`SqliteIndexStorage.cpp:440-445`) and modifiers OR-ed together (`:447-460`).
   The scar is already in the tree: `src/lib_core/utility/utilityMainFunction.inl:15`
   cites the upstream bug ("Nodes for different symbols with the same name are
   merged") and hacks around it for `main` alone.
3. **The provenance is thrown away.** Every indexer knows, at index time, which
   language and which source group it is serving — `IndexerCommand::m_sourceGroupId`
   (`IndexerCommand.h:106`, tagged at `CombinedIndexerCommandProvider.inl:50`) and
   `IndexerCommandType`. Neither ever reaches `ParserClient`, so neither reaches
   storage. The only language marker that survives is `file.language`, and it is
   first-writer-wins in SQLite (`SqliteIndexStorage.cpp:535-558` inserts nothing
   for a path that already exists) while being last-writer-wins in memory
   (`IntermediateStorage.inl:205-208`) — the two disagree for exactly the files
   two indexers both touch.

So a cross-language graph today is one where the seams are invisible, some of
their symbols were discarded, and some unrelated symbols were merged. The purpose
of this design is to make the seam a **thing you can select, filter, and look at**.

## What a boundary is

> **A boundary is two declarations, in different languages, denoting one runtime
> entity.** It is an *edge*, never an identity merge.

That rule is the whole design. Identity merge is what we already do accidentally,
and it destroys the very information we want: after a merge there is one node and
no seam. File nodes are the single deliberate exception — every indexer emits
`"/\tm<path>\ts\tp"`, a file touched by two languages is one node, and that stays
true (§Decision 4).

Three species occur in this repository, which is the natural first subject:

| Species | Mechanism | Example here |
|---|---|---|
| **ABI-mediated** | C symbol names agreed by the linker | `turso_shim`'s `tsq_*` C API consumed from C++; thoth-ipc's C surface |
| **Schema-mediated** | one declaration generates types in N languages | `abi-schemas/ipc-indexer/*.fbs` → C++, Rust, Swift and Zig mirrors |
| **Build-mediated** | one build system pulls another language's headers in | Zig's `@cImport` of flatcc (`src/zig_indexer/src/ipc/c.zig:5`) |

The schema-mediated case is the one Sourcetrail is worst at and this fork most
needs: four `StorageNode` types in four languages, generated from one `.fbs`
table, related to each other by nothing the graph can see.

## Decision 1 — Provenance is recorded, not derived

Add an inline `node.languages` column carrying a **`LanguageMask`** (bit per
`LanguageType`, `LanguageType.h:17-25`), written by every producer, **OR-merged
across producers** exactly as `modifiers` already is (`SqliteIndexStorage.cpp:447-460`).

*Rejected: deriving language by joining occurrence → source_location → file.language.*
It is free of schema change and wrong in the cases that matter: reference-only
nodes carry no definition location, the ancestor scope nodes minted by
`ParserClientImpl::addNodeHierarchy` (`ParserClientImpl.inl:235-262`) have no
location at all, and the graph path would pay a join per node.

*Rejected: `node_attribute`.* Its own header scopes it to display-only facts and
its only reader is the tooltip; language is queried on the hot path, which
`DESIGN_STORAGE_CODEGEN.md` says stays a typed column or an edge. This is the
inline-column case the facet contract already sanctions, next to `type`,
`modifiers` and `access`.

The OR-merge is not incidental — it is the diagnostic. **A node with two language
bits is a node two languages both claim**, which is either a real boundary, a file
(expected), or an accidental name collision. One `WHERE languages & (languages-1) != 0`
turns defect (2) above from an invisible corruption into a measurable list. We
should measure it before deciding what to do about it (§Decision 4).

## Decision 2 — The boundary is a node: the contract atom

A boundary gets its own node — a **contract atom** — and each participating
declaration links to it with a new `EDGE_BINDS` edge kind (`Edge.h` uses 13 bits,
max `1<<12`; there is headroom).

```
C++  void tsq_open(...)   ──EDGE_BINDS──▶  ┌─────────────────┐
Rust extern "C" tsq_open  ──EDGE_BINDS──▶  │ abi:tsq_open    │
Zig  export fn tsq_open   ──EDGE_BINDS──▶  └─────────────────┘
```

Why a node and not an edge between the declarations:

- **n participants cost n edges, not n².** Four languages sharing one FlatBuffers
  table is 4 edges to one atom, not 6 edges between peers — and the atom is what
  you select to see the whole seam at once.
- **It is the thing the question is about.** "What crosses here" is a node
  activation, which the existing graph, code view and search already know how to
  do; no new interaction model.
- **The precedent exists.** `DESIGN_NODE_MODIFIERS.md` Axis 3b already sketches
  non-declaration entities as nodes with edges to them ("the target of a config
  guard is never a declaration"). An ABI symbol is outside the sources in exactly
  the same sense as `feature="serde"`.
- **The representation exists.** An atom is a symbol nobody defines: `DefinitionKind::NONE`,
  the settled treatment for referenced-but-undefined symbols
  (`ROADMAP_RUST_INDEXER.md:295-301`, `PARITY_ZIG_INDEXER.md:180`).

Direction is a contract, not a preference: **declaration → atom**, uniformly, for
every producer. `ROADMAP_SWIFT_INDEXER.md:306` already settled the precedent that
when two indexers can emit the same relation they must agree on direction; this
one binds four. That makes it ADR material — see X5.

## Decision 3 — Resolution rides the existing name merge, in a reserved namespace

Two indexers that independently emit an atom for the same ABI symbol must end up
on **one** node. That is exactly what `serialized_name` dedup already does — the
mechanism that causes defect (2) — used deliberately for the one case where a
merge is the intent.

Atoms therefore live in a **reserved name namespace**, minted by a new
`NameDelimiterType` (`abi`, `schema`) so an atom name can never be spelled by an
ordinary declaration:

```
abi\tm    tsq_open\ts\tp
schema\tm Sourcetrail.Ipc\tnStorageNode\ts\tp
```

No resolver, no post-pass, no Datalog: the C++ indexer and the Rust indexer each
emit `abi:tsq_open`, and the existing merge makes them the same node with both
language bits set. Derived links that need *inference* rather than name agreement
(matching by signature, following a build graph) remain the province of
`ROADMAP_ANALYSIS_ENGINES.md` phase 1 and are out of scope here.

Two consequences to respect:

- **Zig must not prefix atom names.** `storage.zig:151-158` prefixes every symbol
  with its defining file path to avoid collisions; that is right for Zig
  declarations and fatal for atoms, which must be spelled identically by every
  producer.
- **Only declared linkage mints an atom.** A bare function named `init` or `free`
  must not join a boundary. The atom is created from *evidence of linkage* —
  `extern "C"`, `#[no_mangle]`/`extern "C"`, `export`/`extern`, `@_cdecl` — never
  from a name that merely looks foreign.

## Decision 4 — Identity hygiene is a separate, evidence-led change

The obvious companion change is to language-qualify ordinary symbol names so Rust
and Swift stop sharing C++'s `::` namespace. **It is deliberately not part of this
design**, for two reasons: it is the single riskiest edit in the system (it
changes the identity of every node in every index, and `serialized_name` is the
join key for the entire storage layer), and we currently have **no measurement of
how often it actually bites**.

Decision 1 supplies that measurement for free. Once `node.languages` exists, a
four-language index of this repository answers the question directly: every node
with two language bits that is neither a file nor an atom is a collision. Land X0,
index, count, then decide — and if the count justifies it, that change gets its
own design and its own ADR.

Files stay language-neutral regardless. They are the one join we want.

## Decision 5 — Visualization: three affordances

Language must reach the GUI first: it currently stops inside
`PersistentStorage::m_fileNodeLanguage` (`PersistentStorage.h:349`), whose accessor
is private (`PersistentStorage.cpp:3106-3115`) and whose only consumers set the
syntax highlighter. With Decision 1 the fact is on the node, so what is needed is
one new `StorageAccess` query (plus `StorageAccessProxy` and `StorageCache`) and
one field on the view model.

**(a) Language as a visual dimension, not a node kind.** `NodeKind` is a bitmask
of 23 kinds and language is orthogonal to kind — encoding `RustFunction` would
square the vocabulary. Style it the way deprecation is styled: an orthogonal
override applied after the type-derived style, the working precedent at
`QtGraphNodeData.cpp:107-118`. Colour-scheme keys follow the existing
`graph/node/<type>/…` convention with the `/like` aliasing mechanism
(`ColorScheme.inl:46`) so the seven schemes need one block each, not a rewrite.

**(b) Group by language.** `GroupType` (`GroupType.h:6-14`) gains `LANGUAGE`, and
`groupNodesByParents` (`GraphController.cpp:1656-1798`) gains a third bucket
alongside `FILE` and `NAMESPACE` — the machinery is already there, including the
group node type, styling hook and layout. The UI cost is real but contained: the
grouping control (`QtGraphView.cpp:180-224`) is a mutually-exclusive two-button
pair and needs to become three.

**(c) The boundary view.** Activating an atom shows every declaration bound to it,
grouped by language — this is the feature. It needs one graph query ("nodes with
`EDGE_BINDS`") and a filter chip for "crosses a language boundary". Note the chip
cannot be a `NodeTypeSet` filter: those matchers receive only a `NodeType`
(`NodeTypeSet.h:41-45`), so this is a new `SearchMatch::CommandType`, not a new
node kind. The legend (`GraphController.cpp:2559`) gains a boundary section.

## Non-goals

- **No new side table.** Everything here is an inline column, an edge kind, or a
  node — per `DESIGN_STORAGE_CODEGEN.md`.
- **No signature checking across languages.** v1 links by declared linkage name.
  Verifying that Rust's `extern "C" fn tsq_open(*const c_char)` and C's
  `tsq_open(const char*)` agree on their types is a different project.
- **No build-system inference.** We record what a declaration *says* (`extern "C"`,
  `@cImport`), not what the linker actually resolved.
- **No cross-language refactoring, renaming, or navigation-by-edit.**
- **No migration.** Per house rule, a storage change is a version bump and a fresh
  re-index (`isIncompatible()` → `PROJECT_STATE_OUTVERSIONED`).
- **Not the identity rewrite** (Decision 4).

## Staged sequence (plan of record)

Each stage is independently landable and independently verifiable. Groundwork
lands alone, proven behaviour-preserving, before any feature rides on it.

- **X0 — provenance, end to end, no visible change.** `node.languages` column;
  `language_mask` field on `StorageNode` in `intermediate_storage.fbs`; serializer;
  `Storage::inject`; OR-merge in `SqliteIndexStorage::addNodes`; thread
  `IndexerCommandType` from `IndexerCommand` through `Indexer<T>::index` into
  `ParserClientImpl` so every producer stamps its own language. **Forces the
  storage version bump** (27 → 28); do it once, here. *Verification: a
  single-language index is byte-identical to today except for the new column; a
  four-language index of this repo shows the expected per-language node counts.*
- **X1 — measure the collisions. ✅ DONE (2026-08-15).** See *X1 executed* below.
- **X2 — atoms and the ABI species.** `NameDelimiterType::ABI`; `EDGE_BINDS`;
  producers for C++ `extern "C"`, Rust `#[no_mangle]`/`extern "C"`, Zig
  `export`/`extern`, Swift `@_cdecl`. Includes lifting the Swift filter at
  `SemanticIndexer.swift:120` so C symbols in a mixed target survive, and fixing
  the missing `INDEXER_COMMAND_ZIG` in `InterprocessIndexer.inl:104-108` (today a
  C++ subprocess can pop a Zig command and drop it).
- **X3 — the schema species.** `NameDelimiterType::SCHEMA`; atoms minted from
  FlatBuffers table names by the four generated-code producers. *Open: whether the
  `.fbs` file should instead be indexed as a source group, making the atom a real
  declaration with a definition rather than a synthetic node — see Open questions.*
- **X4 — visualization.** `StorageAccess::getLanguagesForNodeIds`; the node style
  override; `GroupType::LANGUAGE`; the boundary filter chip; the legend section.
- **X5 — the invariant.** ADR-0009 recording the two rules every indexer must obey
  forever: *a boundary is an edge, never an identity merge* (files excepted), and
  *`EDGE_BINDS` points declaration → atom*. ADRs bind all four producers, which is
  what ADR-0002 and ADR-0003 already do.
- **X6 (optional) — build-mediated boundaries.** Zig `@cImport` resolving to the
  C header's symbols; bridging headers. Needs a clang parse from a non-C++
  indexer, so it is genuinely harder than X2/X3 and deliberately last.

## X0 executed (2026-08-15) — `node.languages`, storage v28

Landed as designed: an OR-merged inline mask, stamped by all four producers,
with the C++ side taking its bit from the `IndexerCommandType` that selected the
indexer. 733 C++ test cases / 2,593 assertions, 116 Rust and 25 Zig tests green;
a Zig index reproduced its previous node and edge counts exactly (2,884 /
11,082), which was the no-visible-change gate.

Two things the work turned up, neither of them predicted here:

- **Zig's standalone build never regenerated its flatcc bindings when a schema
  changed.** `build.zig` passed the schema directory as an opaque string, so no
  `.fbs` content was in the step's cache key; the symptom was an arity mismatch
  in `wire.zig`, one layer from the cause. Schemas are declared as step inputs
  now. Anyone editing `abi-schemas/` before this would have been building against
  stale bindings.
- **Nodes minted outside any indexer carry no bit,** and should. File rows for
  sources nobody indexed are created by the main process, which has no producing
  language; a mask of 0 is the honest answer there, and it is the one place 0
  means something rather than missing.

## X1 executed (2026-08-15) — measured: rare in the wild, real in principle

The question Decision 4 defers to: **how often do two languages silently collide
on one name?**

**In real code, in this sample: never.** A two-group index of this repository's
`src/lib_aidkit` (C++) and `src/agent_mcp_bridge` (Rust) — 958 C++ nodes, 2,574
Rust nodes — produced **zero** shared nodes. A C++/Zig pairing produced zero as
well, though that one proves less: Zig prefixes every symbol with its defining
file path, so it cannot collide with anything by construction.

**In principle, yes, and now demonstrably.** A constructed minimal case — a
global-scope `struct Widget` in C++ and a crate-root `pub struct Widget` in Rust
— merges into **one node**, which the new mask reports as `languages = 3`
(`cxx|rust`). That is the defect this design was written about, caught by the
column that X0 added.

The interesting part is *why the real corpora were clean*, because it is not the
reason one would guess:

| Symbol kind | Collides? | Why |
|---|---|---|
| Functions | No | C++ serializes the signature into the name (`shared_thing\tsint\tp(int)`); Rust emits empty signature parts (`shared_thing\ts\tp`). The names differ even when the symbol names match. |
| Fields | No | Same reason — the C++ field carries its type in the signature part. |
| **Types** | **Yes** | A struct/class has no signature. `::\tmWidget\ts\tp` is byte-identical from either producer. |

So the exposure is narrower than feared and sharper than "names might clash": it
is **types with identical fully-qualified names**, which is likeliest at crate or
namespace root, and it is invisible today in exactly the way a merge is.

**Decision 4 stands: no identity rewrite.** Zero occurrences across 3,532 nodes
of real two-language code does not justify changing the identity of every node in
every index. The finding that makes it safe to defer is that the mask now makes
the failure *visible*: any project can run one query and get its own number
rather than inheriting this one. Revisit if a real project reports a non-zero
count, and note that X2's atoms deliberately rely on the same merge — in a
namespace where merging is the intent.

## Verification

The subject is this repository. It is a four-language program joined by all three
boundary species, which makes it both the test case and the proof.

1. **Index Sourcetrail-TS with four source groups** (C++, Rust, Swift, Zig) into
   one database — the multi-group substrate for this already exists and is
   complete (`DESIGN_MULTIGROUP_FANOUT.md` S0–S5).
2. **X0 gate:** every node carries exactly the language bits of the indexers that
   produced it; single-language indexes unchanged; `file` rows keep their existing
   `language` values.
3. **X2 gate:** `abi:` atoms exist for the `tsq_*` surface and for thoth-ipc's C
   entry points, each with `EDGE_BINDS` edges from at least two languages;
   activating one shows both sides.
4. **X3 gate:** `schema:Sourcetrail.Ipc.StorageNode` binds the C++, Rust, Swift and
   Zig mirrors of that FlatBuffers table — one atom, four edges, four language
   bits.
5. **Regression gate:** the existing suites stay green
   (`ctest --preset llvm-clang-reldbg`, `zig build test` in `src/zig_indexer`,
   `cargo test` in `src/rust_indexer`, the Swift package tests).
6. **Honesty gate:** X1's collision count is recorded here whatever it says,
   including if it says the identity problem is negligible.

## Top risks

1. **The atom namespace leaks.** If an ordinary declaration can ever be spelled
   with the `abi`/`schema` delimiter, a boundary atom merges with a real symbol and
   `type` widening corrupts it. *Mitigation: the delimiter is minted only by the
   atom constructor; assert it in the producers' unit tests.*
2. **ABI names are not unique.** `init`, `free`, `main` exist in every C library.
   *Mitigation: only declared linkage mints an atom (Decision 3); if that proves
   insufficient, scope the atom by the artifact that exports it — which requires
   build knowledge and would move to X6.*
3. **X0 touches every producer and the wire format.** A missed producer means
   silently unlabelled nodes. *Mitigation: X0 lands alone with a per-language count
   assertion; the `.fbs` field is appended, never renumbered.*
4. **The Swift un-filter changes Swift indexes.** Lifting
   `SemanticIndexer.swift:120` admits C and ObjC symbols that were previously
   dropped, which will move node counts in the Swift suite. *Mitigation: land it
   inside X2 with the counts updated deliberately, not as a drive-by.*
5. **Grouping UI is a two-state control.** A third grouping mode is a real UI
   change, not a parameter. *Mitigation: X4 can ship styling and the boundary
   filter without grouping if the control fight is not worth it.*

## Open questions

1. **Should the schema atom be synthetic at all?** Indexing `abi-schemas/*.fbs` as
   its own source group would make the contract a *defined* symbol with a file, a
   location and a code view — strictly better than a synthetic node, at the cost of
   a small `.fbs` parser. This may make X3 both easier and more valuable.
2. **Does `language` belong on `symbol` rather than `node`?** Scope nodes minted by
   `addNodeHierarchy` have no language of their own and would inherit their
   children's bits by OR — arguably right, arguably noise.
3. **What does a two-language node mean in the UI before X1 answers the collision
   question?** Until then it is ambiguous by construction, and the boundary filter
   should probably show only atoms.
4. **Turso mirror.** `ConcurrentTursoWriter` keeps first-seen values where SQLite
   widens (`DESIGN_MULTIGROUP_FANOUT.md` risk 3). An OR-merged mask must be
   OR-merged in both writers or the two backends diverge on exactly the nodes this
   design is about.

## Critical files

- `src/lib_core/data/storage/type/StorageNode.h` — the row that gains `languages`.
- `abi-schemas/ipc-indexer/intermediate_storage.fbs` — the wire contract; one
  appended field.
- `src/lib_core/data/indexer/interprocess/serialization/IntermediateStorageSerializer.inl` — pack/unpack.
- `src/lib_core/data/storage/Storage.inl:28-42` — the id-remapping merge.
- `src/lib_core/data/storage/sqlite/SqliteIndexStorage.cpp:398-505` — dedup, type
  widening, the OR-merge that Decision 1 extends.
- `src/lib_core/data/name/NameHierarchy.inl:9-12`, `NameDelimiterType.h` — the
  reserved atom namespace.
- `src/lib_core/data/graph/Edge.h:27-43` — `EDGE_BINDS`.
- `src/lib_core/data/parser/ParserClient.h`, `ParserClientImpl.inl` — the producer
  API every indexer calls.
- `src/lib_cxx/data/parser/cxx/CxxIndexingContext.inl`,
  `src/rust_indexer/indexer/src/parser/collector.rs`,
  `src/swift_indexer/Sources/SourcetrailSwiftIndexerCore/SemanticIndexer.swift:120`,
  `src/zig_indexer/src/storage.zig:112-158` — the four producers.
- `src/lib_core/data/storage/StorageAccess.h`, `StorageAccessProxy`, `StorageCache`
  — the query seam the GUI needs.
- `src/lib_core/component/controller/GraphController.cpp:1656-1798` (grouping),
  `src/lib_gui/qt/graphics/graph/QtGraphNodeData.cpp:107-118` (the style-override
  precedent), `src/lib_core/component/view/helper/GroupType.h`,
  `bin/app/data/color_schemes/*.json`.
