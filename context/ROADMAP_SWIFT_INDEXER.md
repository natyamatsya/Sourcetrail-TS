# Roadmap: Swift Language Indexer

## Goal

Bring `sourcetrail_swift_indexer` from a transport-only stub to full parity with
the C++/Rust indexing pipeline: a standalone macOS binary that indexes Swift
packages over the existing thoth-ipc / FlatBuffers protocol, with a **hybrid
engine** — semantic data from the compiler-produced index store (IndexStoreDB)
plus a SwiftSyntax declaration fallback for code that is not (yet) buildable.

The staged design and rationale for the shipped engine live in
[DESIGN_SWIFT_INDEXER.md](DESIGN_SWIFT_INDEXER.md) (SW0–SW10). The planned
semantic-enrichment series (SW11–SW16) is specified in this file below. This
file tracks status at a glance.

---

## Status (2026-07-18)

| Stage | What | State |
|-------|------|-------|
| SW0 | Build revival: flatc Swift codegen migration, Core library + test target, `BUILD_SWIFT_LANGUAGE_PACKAGE=ON` in macOS presets | ✅ done |
| SW1 | Package model (`swift package describe`) + build driver + diagnostics → non-fatal errors + per-file progress | ✅ done |
| SW2 | Semantic core: IndexStoreDB → nodes/edges/occurrences, USR→NameHierarchy resolution | ✅ done |
| SW3 | Syntactic fallback (SwiftSyntax) + hybrid merge (freshness rule) | ✅ done |
| SW4 | Robustness: chunked push, shared 200-failure retry budget | ✅ done |
| SW5 | Host-side per-package command emission; options fields (`swift_build_args` / `swift_toolchain_path` / `swift_index_store_path`) + settings mixin + BuildDriver consumer; Rust `equalsSettings` bugfix | ✅ done |
| SW6 | K Swift supervisor fan-out | ✅ done |
| SW7 | Package-granular shard striping (Rust + Swift) | ✅ done |
| SW8 | GUI wizard for Swift options (`QtProjectWizardContentSwiftOptions`) | ✅ done |
| SW9 | `index_self` smoke binary, docs | ✅ done |
| SW10 | Code-view parity: SCOPE locations + precise token extents via universal SwiftSyntax | ✅ done (local symbols / qualifiers follow-up) |

The engine is complete and exercised end to end. `swift run index_self` on the
indexer's own package produces ~30 files / ~2k nodes / ~7.8k edges / ~13k
occurrences (the occurrence jump over pre-SW10 is the added SCOPE locations).

The **SW11–SW16 series (all landed 2026-07-18)** raises the graph from a
generic-OO projection (which any language would produce) to one that speaks
Swift: generics/constraints, protocol conformance fidelity, concurrency
isolation, attribute-driven relations, macros, and API-surface metadata. Two
axes are schema-deferred (they need an intermediate-storage field that ripples to
the C++/Java sides): concurrency's actor-identity / `async` (SW13) and the
`package` access level / `open`-vs-`public` distinction / `@available` (SW16).

| Stage | What | State |
|-------|------|-------|
| SW11 | Generic-parameter tier + constraints + type-argument use-sites (the Rust-lifetime analog) | ✅ done |
| SW12 | Protocol conformance fidelity: witnesses, conditional/retroactive, default impls | ✅ done |
| SW13 | Concurrency model: global-actor isolation, `Sendable` (actor identity / `async` schema-deferred) | ✅ done |
| SW14 | Attribute-driven relations: property wrappers + result builders | ✅ done |
| SW15 | Macros: attached + freestanding applications | ✅ done |
| SW16 | API-surface metadata: access control (`@available` deferred) | ✅ done |

---

## Semantic enrichment — SW11–SW16 (planned)

### Motivation & the schema windfall

The Rust indexer represents lifetimes not as a special case but as its
**generic-parameter tier**: each lifetime and type param is a
`NODE_TYPE_PARAMETER` member of its owner, and bounds (`T: Trait`, `'a: 'b`)
become edges (`collector.rs` `bound_owner` → `EDGE_INHERITANCE`/usage). The
Swift graph today has **no generic-parameter tier at all** — `baseOf` collapses
class inheritance and protocol conformance into one `EDGE_INHERITANCE`, actors
are bare `NODE_CLASS`, extensions vanish, and attributes are invisible. SW11–16
close that gap along the axes that make Swift *Swift*.

**Key finding that de-risks most of this:** Sourcetrail's edge/node vocabulary
already carries the concepts, so there is **no cross-language schema migration** —
only exposing constants that `StorageKinds.swift` mirrors from `graph/Edge.h`
and `NodeKind.h` (wire values already agreed tri-language):

| Concept | Existing wire kind | Swift-side status |
|---------|--------------------|-------------------|
| Type parameter node | `NODE_TYPE_PARAMETER` (`1<<17`) | defined in `StorageKinds.swift`, **unused** |
| Type argument edge | `EDGE_TYPE_ARGUMENT` (`1<<6`) | not yet exposed |
| Specialization edge | `EDGE_TEMPLATE_SPECIALIZATION` (`1<<7`) | not yet exposed |
| Macro application | `EDGE_MACRO_USAGE` (`1<<11`) + `NODE_MACRO` (`1<<19`) | node exposed, edge not |
| Attribute application | `EDGE_ANNOTATION_USAGE` (`1<<12`) | not yet exposed |

Swift attributes (`@MainActor`, `@State`, `@ViewBuilder`, `@available`, attached
macros) are literally *annotations* — `EDGE_ANNOTATION_USAGE` is the natural home
for SW13/SW14 and the attribute half of SW15/SW16. The only genuinely new work
is Swift-side extraction (mostly SwiftSyntax, extending the SW10
`DeclScopeMap`/enrichment plumbing) and USR resolution of the edge targets.

### Engine split (recurring theme)

- **SwiftSyntax (syntactic)** owns declaration-shape facts that the index store
  does not model as relations: generic-param lists, `where` clauses, the `actor`
  keyword, `async`/`nonisolated`, attribute *applications*, access modifiers,
  `@available`. Reliable even on broken builds (rides the hybrid fallback).
- **IndexStoreDB (semantic)** owns cross-symbol resolution: the USR of a
  constraint's target protocol, protocol-witness relations (`overrideOf`),
  conformance (`baseOf`), macro-definition USRs and expansion roles.
- Each stage is therefore **hybrid**: SwiftSyntax finds the site and spelling,
  the index resolves the target node. Where the target can't be resolved
  (external / not built), fall back to a module-qualified flat name — the same
  degradation the USR→NameHierarchy resolver already uses.

---

### SW11 — Generic-parameter tier + constraints (L, centerpiece)

**Core landed 2026-07-18** — parameter nodes + bounds. `GenericSyntax.swift`
(`GenericParamMap`) extracts, per file, each owner's generic parameters and the
constraints on its own params (inline bounds + `where` conformance/same-type),
keyed to source positions. The **syntactic** pass emits `NODE_TYPE_PARAMETER`
members with precise name tokens for every generic owner (works on broken
builds). The **semantic** pass additionally emits the bound edges: after the
occurrence loop, `emitGenerics` joins the syntactic params to the owner it
resolved (`defPartsByPos`) and each constraint target to the store's already-
resolved reference (`refTargetByPos`) — conformance/class bound → `EDGE_INHERITANCE`,
same-type → `EDGE_TYPE_USAGE` — and suppresses the default container→type edge at
constraint positions so the bound attaches to the parameter. Swift has no
dedicated index symbol kind for generic params, so `StorageBuilder.setNodeType`
forces `typeParameter` at the authoritative declaration. Covered by
`GenericsTests` (syntactic members; semantic params + inline/where/same-type
bounds).

**Type-argument use-sites landed 2026-07-18.** `GenericArgMap` (also in
`GenericSyntax.swift`) maps each direct type argument's base-identifier position
to its generic base type's position. The semantic pass builds a per-file
position→symbol index and, at a type-argument site, emits `EDGE_TYPE_ARGUMENT`
from the enclosing declaration to the argument (replacing the plain
`EDGE_TYPE_USAGE`) instead — gated by a tri-state **specialization scope**:
`off` (none), `all` (every application), `local` (default — only applications
whose base type has a non-system definition, so stdlib containers like
`Array<Int>` don't flood the graph). The scope is a real end-to-end knob,
plumbed exactly like the SW5 options: `swift_specialization_scope` in
`indexer_command.fbs`, round-tripped through all three pop-rewrites (each with a
test), a `SourceGroupSettingsWithSwiftOptions` field, host emission in
`SourceGroupSwift`, and an Off/Local/All combo in the GUI wizard. Uses the
existing `EDGE_TYPE_ARGUMENT` wire kind — no schema change. Covered by
`GenericsTests` (local keeps package base only; all includes stdlib; off emits
none). Sugar forms (`[Int]`, `T?`) have no written base token and stay plain
type usages (documented limitation).

The direct analog of Rust lifetimes, and the biggest single parity gap.

- Emit `NODE_TYPE_PARAMETER` for every generic parameter of a type/func/subscript,
  as a `MEMBER` of its owner (mirrors `collector.rs::emit generic params`), with
  precise SwiftSyntax name extents (SW10 machinery).
- **Constraints** from `where` clauses and inline bounds:
  - conformance bound `T: Collection` → edge param→protocol
    (`EDGE_INHERITANCE`, matching Rust's bound handling);
  - same-type constraint `where T.Element == U` → `EDGE_TYPE_USAGE` between the
    two sides (Swift-richer than Rust; no lifetime equivalent);
  - class bound `T: SomeClass` → `EDGE_TYPE_USAGE`.
- **Type arguments** at use sites (`Foo<Bar>`, `Array<Element>`) → `EDGE_TYPE_ARGUMENT`
  from the enclosing decl to `Bar` — the concept Rust models with its
  specialization scope. Gate volume behind the same tri-state
  specialization-scope setting the Rust side exposes (off/local/all) to avoid
  node blow-up; reuse `SourceGroupSettingsWithSwiftOptions` for the knob.
- *Extract:* param lists + where clauses are pure SwiftSyntax; constraint/arg
  targets resolve through the index (flat-name fallback when external).
- *Test:* a generic type with a multi-clause `where`, a conditional extension,
  and a same-type constraint — assert param nodes + each constraint edge.

### SW12 — Protocol conformance fidelity (M)

**Landed 2026-07-18.** An audit found IndexStoreDB already gives most of this via
the relations SW2 handles: conformance base edges (type → protocol as
`EDGE_INHERITANCE`, including the *conditional* conformance's base edge) and
**witness edges** (a concrete member → the requirement it satisfies as
`EDGE_OVERRIDE`, for methods *and* properties — Swift's index carries requirement
satisfaction on the `overrideOf` relation, so no new resolution was needed). Two
real gaps were closed, both subprocess-only (no schema/plumbing change):

- **Conditional-conformance constraints:** `extension Pair: Greeter where T:
  CustomStringConvertible` now emits the `where` bound. `GenericSyntax.swift`
  gained `ExtensionConstraint` extraction (SW11 skipped extension `where`
  clauses); `SemanticIndexer.emitExtensionConstraints` resolves the extended
  type at its reference position and attaches the bound to *its* parameter node
  (`Demo::Pair::T` → `CustomStringConvertible`, `EDGE_INHERITANCE`), reusing the
  SW11 constraint machinery.
- **Default-implementation self-loop suppressed:** a default `greet()` in
  `extension Greeter` resolves to the same node as the requirement it satisfies,
  which produced a useless `OVERRIDE: Greeter::greet() → Greeter::greet()`; the
  override emission now skips `source == target`.

Covered by `ConformanceTests` (conformance + method/property witnesses +
conditional constraint + no self-loop). *Limitation:* the conditional bound
attaches to the type's parameter node unconditionally — Sourcetrail has no
conformance-scoped constraint slot, so it reads as "T is constrained" rather than
"…for this conformance". Default impls collapse into the requirement node (a
defensible unification: the default *is* the protocol's member).

Make Swift's defining feature first-class instead of merged into inheritance.

- **Witness edges** — which concrete member satisfies which protocol requirement.
  Swift's index rides this on the `overrideOf` relation, so audit first: SW2
  already emits `EDGE_OVERRIDE` from `overrideOf`, which likely *already* yields
  partial requirement-satisfaction edges. Confirm coverage, then ensure protocol
  requirements (not just class overrides) resolve as targets. "Who implements
  this requirement?" is premium navigation.
- **Conditional / retroactive conformance** (`extension Array: Foo where …`):
  emit the conformance `EDGE_INHERITANCE` *plus* the constraint edges from SW11's
  where-clause handling, so the graph shows *when* the conformance holds.
- **Default implementations** in protocol extensions: a method in
  `extension P { … }` that matches a requirement → `EDGE_OVERRIDE` to the
  requirement (semantic) so defaults are discoverable from the requirement.
- *Non-goal:* a distinct "conformance" edge kind — Sourcetrail has no such slot;
  `EDGE_INHERITANCE` + the constraint edges carry it. Note the limitation.

### SW13 — Concurrency model (M)

**Landed 2026-07-18.** An audit against the storage model (`nodes` are only
`id`/`type`/`serializedName` — no modifier slot) split this cleanly into
representable and not:

- **Global-actor isolation** — `@MainActor` / custom `@globalActor` on a
  declaration is an `EDGE_ANNOTATION_USAGE` to the actor type, via the SW14
  walker. SW13 fixed the **type-level** case (`@MainActor class ViewModel`): the
  index emits the reference but attributes it to module scope, so no edge was
  made. `AttributeMap` now also records each attribute's **annotated
  declaration** (nearest declaration ancestor via SwiftSyntax), and
  `SemanticIndexer.emitAnnotationUsage` sources the edge from it — so both
  `ViewModel → MainActor` and `run() → MainActor` flow. This also hardened SW14
  (property wrappers on any declaration) and pre-wires SW16.
- **`Sendable`** needs no special work — it is a protocol conformance, so SW12
  already emits `Token → Sendable` as `EDGE_INHERITANCE`.
- **Deferred (schema-bound):** *actor identity* (an `actor` is `NODE_CLASS`,
  indistinguishable from a class — the index emits no implicit `Actor`
  conformance, and there is no node-modifier slot) and *`async`/`nonisolated`*
  (function modifiers with no storage slot and no type reference to hang an edge
  on). Both need an intermediate-storage node-modifier field + PersistentStorage
  inject + UI support — out of scope until the schema grows one.

Covered by `ConcurrencyTests` (type- and function-level isolation + Sendable).

Swift's most distinctive modern axis, and safety-critical.

- **Actor identity:** keep `NODE_CLASS` (no schema slot for actors) but record
  actor-ness so the code view / hover can label it; revisit a dedicated kind only
  if the schema ever grows one (tracked in *Known limitations*).
- **Global-actor isolation:** `@MainActor` and custom global actors on a decl →
  `EDGE_ANNOTATION_USAGE` decl→actor-type. This is a genuinely new *relationship*
  dimension (cross-cutting isolation), and it lands on an existing edge kind.
- **`Sendable`** needs no special work — it is a protocol conformance and falls
  out of SW12 for free (call this out so it isn't re-implemented).
- **`async` / `nonisolated`** as decl metadata (syntactic), surfaced in the code
  view rather than as edges.
- *Extract:* attributes + `actor`/`async`/`nonisolated` keywords are SwiftSyntax;
  the global-actor *type* resolves through the index.

### SW14 — Attribute-driven relations: property wrappers & result builders (M)

**Landed 2026-07-18.** An audit showed the index already resolves a custom
attribute's type reference at the attribute's name position, with the annotated
declaration as the containing symbol — it was just emitted as a plain
`TYPE_USAGE`. So SW14 is small: `AttributeSyntax.swift` (`AttributeMap`) marks
every attribute-name position (the reusable walker SW13/SW16 also consume), and
`SemanticIndexer` emits `EDGE_ANNOTATION_USAGE` there instead — but only when the
target resolves to a *type* node (`isTypeNodeKind`), so attached macros fall
through for SW15 and built-in attributes (`@available`, `@objc`), which name no
type, never match. The override is scoped to the attribute position, so a
declaration's ordinary types stay `TYPE_USAGE` (`@Clamped var level: Int` →
annotation-usage to `Clamped`, type-usage to `Int`). Uses the existing
`EDGE_ANNOTATION_USAGE` wire kind — no schema change. Covered by `AttributeTests`
(property wrapper + result builder become annotation usages; ordinary types
unaffected). Global-actor attributes (`@MainActor`, custom `@globalActor`) already
flow through the same path — SW13 builds the actor-identity/`async` metadata on
top.

The features that make SwiftUI/Combine/SwiftData code navigable.

- **Property wrappers** (`@State`, `@Published`, `@Binding`, custom
  `@propertyWrapper`): edge wrapped-property → wrapper type via
  `EDGE_ANNOTATION_USAGE`. Distinguishes "used as a wrapper" from an ordinary
  type reference.
- **Result builders** (`@ViewBuilder`, custom `@resultBuilder`) applied to a
  param/func: `EDGE_ANNOTATION_USAGE` to the builder type.
- Generalizes to a reusable **attribute-application walk** in SwiftSyntax
  (shared with SW13's isolation attrs and SW16's `@available`): for each
  attached custom attribute, resolve its type USR and emit annotation-usage. Build
  this walker once; SW13/14/16 are then thin consumers.
- *Extract:* attribute syntax is SwiftSyntax; wrapper/builder type USR via index.

### SW15 — Macros (M/L)

**Landed 2026-07-18.** An audit (`@Observable` on a class) settled the two open
questions: IndexStoreDB *does* report macro references as `.macro` symbols
(`NODE_MACRO`, `1<<19`), and both attached macros and freestanding macros surface
the same way — a reference occurrence whose resolved symbol is a macro. So the
whole feature is one check in `SemanticIndexer.processReference`: when a
reference resolves to `NODE_MACRO`, emit `EDGE_MACRO_USAGE` **from the file node
to the macro node** (with a token occurrence at the use site), matching the
C++/Rust convention (`collector.rs apply_macro_usage_rows`) rather than the
speculative macro→decl direction floated earlier — cross-language edge
consistency wins. The macro node is definition-less (`DEFINITION_NONE`); it lives
outside the indexed sources. Uses the existing `EDGE_MACRO_USAGE` wire kind — no
schema change. This also reclassified what used to be a plain `USAGE` edge at
macro sites. **Expanded members** (the peers/accessors a macro synthesizes, e.g.
`@Observable`'s `_$observationRegistrar`, `access(keyPath:)`) already flow as
ordinary nodes/edges from the compiler's expansion units — no extra work needed.
Covered by `MacroTests` (attached `@Observable` → file-to-macro usage; every
macro-usage edge is file→macro; macros never left as plain usages).

Swift 5.9+ macros — modern and increasingly load-bearing (`@Observable`,
`@Model`, `#Predicate`, `@Test`).

- **Attached macros** (`@Observable`, `@Model`): `EDGE_MACRO_USAGE` from the
  macro `NODE_MACRO` to each decl it annotates (reuses the SW14 attribute walk to
  find the site; a macro attribute is distinguishable from a wrapper by resolving
  its symbol kind to *macro* in the index).
- **Freestanding macros** (`#Predicate`, `#file`): `EDGE_MACRO_USAGE` at the
  expansion site.
- *Optional stretch:* represent **expanded members** (the peers/members a macro
  synthesizes) as implicit nodes — the index store records macro-expansion roles,
  but the source locations are synthetic; likely defer as its own follow-up.
- *Risk:* macro symbols and expansion roles are the least-exercised corner of
  IndexStoreDB; budget audit time. Falls back gracefully to "macro node with no
  edges" (today's behavior) if resolution is thin.

### SW16 — API-surface metadata (S)

**Landed 2026-07-18.** Unlike SW13's actor identity, access control *is*
representable: the intermediate storage already has a `StorageComponentAccess`
table (`node_id`, `AccessKind`), the Swift push already reserved a
`componentAccessesVectorOffset` (previously hardcoded empty), and the app injects
it (C++ emits it). So SW16 populated that slot — the only stage beyond C++ to do
so, since the Rust collector leaves it empty.

`AccessSyntax.swift` (`swiftAccessKind` + `AccessMap`) reads the declared access
purely syntactically (works on broken builds), mapping Swift's six levels onto
Sourcetrail's four `AccessKind`s: `open`/`public` → PUBLIC, `package`/`internal`
(and the implicit default) → DEFAULT, `fileprivate`/`private` → PRIVATE. Emitted
as a `StorageComponentAccess` per node by both passes (semantic by name-position
join, syntactic inline from the decl modifiers). New Swift storage type
+ `StorageBuilder.recordComponentAccess` + serialization, and the `StorageChunker`
now carries each node's access in its home group so chunks stay self-contained.
Enables a "public-API-only" graph filter. Covered by `AccessTests` (semantic
+ syntactic).

*Limitations:* `package` has no dedicated `AccessKind` (collapses to DEFAULT,
same as `internal`), and `open` vs `public` (the subclassable distinction) is
lost — both need an `AccessKind` schema addition shared with the C++/Java sides.
*Deferred:* **`@available`** gating has no representable target — a platform
(`iOS`) is not a node, and there is no cfg-analog/metadata slot; it would need a
new storage concept, out of scope.

Cheap, high-utility decoration — no new edges.

- **Access control** as a node attribute: `open` / `public` / `package` /
  `internal` / `fileprivate` / `private`. Swift's `package` level (5.9) and the
  `open` vs `public` (subclassable) distinction are Swift-specific. Enables a
  "public-API-only" graph filter and honest API-surface views.
- **`@available`** platform/version gating → metadata (the analog of Rust `cfg`
  gating), optionally an `EDGE_ANNOTATION_USAGE` to the platform when a symbol is
  referenced. Purely syntactic, so it works on broken builds too.
- *Extract:* modifiers + `@available` are pure SwiftSyntax — the cheapest stage,
  no index dependency.

### Sequencing & sizing

- **SW11 first** — it builds the type-parameter tier and the where-clause parser
  that SW12's conditional conformance reuses; it is the centerpiece (L).
- **SW14 before SW13/SW16** in practice — its attribute-application walk is the
  shared substrate for global-actor isolation (SW13) and `@available` (SW16).
  Land SW14's walker, then SW13/SW16 are thin (M, then S).
- **SW12** is independent (semantic-only, M) and can slot in any time after SW11.
- **SW15** last (M/L) — highest audit risk, and benefits from the SW14 walk.
- Every stage stays hybrid and degrades to the current coarser graph when a
  target USR can't be resolved, so each lands independently without regressing
  the SW2/SW10 baseline.

---

## Swift options bundle (SW5 fields + SW8 GUI) — landed

The **command option fields** (`swift_build_args`, `swift_toolchain_path`,
`swift_index_store_path`), their **settings mixin**
(`SourceGroupSettingsWithSwiftOptions`, mixed into `SourceGroupSettingsSwiftEmpty`),
and the **GUI wizard** (`QtProjectWizardContentSwiftOptions`, SW8) shipped together
with a live consumer:

- `BuildDriver.SwiftBuildOptions` selects the toolchain's own `swift` +
  `libIndexStore.dylib` (toolchain override), appends `swift_build_args` to
  `swift build`, and — when `swift_index_store_path` is set — **skips the build**
  and indexes the prebuilt store directly (read-only checkout).
- The three IPC fields round-trip losslessly across all three pop-rewrites
  (C++ `IndexerCommandSerializer`, Rust `ipc/command.rs`, Swift
  `SwiftIndexerCommandChannel`), each covered by a round-trip test.

## Known limitations / follow-ups

- **Local symbols & qualifiers** (SW10 follow-up): function-local `let`/`var`
  navigation (`LOCATION_LOCAL_SYMBOL`) and clickable qualified-name segments
  (`LOCATION_QUALIFIER`) are still unemitted. Locals need `-index-include-locals`
  on the build (semantic) or a scoped SwiftSyntax name walk; both are additive
  on top of the SW10 `DeclScopeMap`/enrichment plumbing.
- **Token extents for references** are still approximated by base-identifier
  length (SW10 made *definition* name extents exact via SwiftSyntax; arbitrary
  reference tokens would need a full per-file token map — cheap to add if the
  approximation ever mis-highlights a real reference).
- **Actors** map to `NODE_CLASS` (no schema/GUI churn); revisit if a distinct
  actor node kind is ever wanted. SW13 records actor-ness as metadata without a
  new node kind.
- **Degenerate sharding**: a shard whose file-level stripe is empty emits no
  package commands (more shards than files) — unchanged from the file-level
  behavior.
- **Optional**: a C++ ipc-harness integration test with Swift commands. The
  subprocess is already covered end to end by the package's own real-build
  integration tests, so this is low-value.

## Non-goals

- **Turso from the subprocess.** The app's concurrent Turso writer stays sole
  writer; the subprocess pushes IntermediateStorage over SHM. A Swift-side
  need would go through `turso_shim`'s `tsq_` C API, not a second Turso SDK.
- **Non-macOS.** The package pins `.macOS(.v14)` and uses `Darwin.POSIX` +
  IndexStoreDB's toolchain `libIndexStore.dylib`.
