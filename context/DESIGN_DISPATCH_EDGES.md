# Design: dispatch edges — class-hierarchy analysis as a pass over the index

- **Status:** shipped (2026-11). The first derived relation in the index, and the smaller
  first cut of "dispatch resolution via CHA" in
  [ROADMAP_ANALYSIS_ENGINES.md](ROADMAP_ANALYSIS_ENGINES.md) (Tier 2): no Datalog engine,
  one SQL pass (`WITH RECURSIVE`) over the stored edges after indexing.
- **Code:** `SqliteIndexStorage::deriveDispatchEdges` / `removeDispatchEdges`
  (`src/lib_core/data/storage/sqlite/SqliteIndexStorage.cpp`), called from
  `TaskFinishParsing::report`, `PersistentStorage::clearFileElements` and the `merge`
  command. Tests: `StorageTestSuite.cpp` ("storage derives ...", "storage refresh derives
  ...").
- **Related:** [DESIGN_STORAGE_SCHEMA.md](DESIGN_STORAGE_SCHEMA.md) (the tables and the
  clear path this has to stay out of the way of).

## What it adds

A call to a method that is overridden points at the method it names: `m_renderCtx->submit()`
in bgfx calls `bgfx::RendererContextI::submit`, and nothing in the graph led from there to the
renderers that implement it. Now the caller also has an `EDGE_DISPATCH` edge to every override
the call may run: `RendererContextMtl::submit`, `RendererContextVK::submit`, ... Drawn as a
call, dashed; its tooltip says "possible dispatch to an override".

## Decisions

### A new edge kind, not CALL with a marker

`Edge::EDGE_DISPATCH = 1 << 14`, readable name `dispatch`. Reusing `EDGE_CALL` with a flag
was the alternative, and it loses on every count:

- A CALL is something the source says. Everything that reads calls — call trails, the
  callers of a function, an agent asking what a function calls, the Ladybug mirror's `Call`
  table — would start mixing derived possibilities with written facts, and every one of them
  would need to learn the flag to tell them apart.
- The flag would need a carrier. Edges have one per-element channel, `element_component`,
  and the graph only loads it for `IS_AMBIGUOUS`; the edge dedup key on the write path is
  `(type, source, target)`, so a derived call and a written call between the same two nodes
  would be one row, and the flag would have to mean "partly derived".
- Removing the derived relation before a recompute is `WHERE type = 16384`. With a marker it
  is a join, and a CALL that is both written and derived cannot be removed at all.

The cost of a new kind is the list of places that enumerate kinds, and each one is handled:
`Edge.h/.inl` (enum, `intToEnum`, readable name, `EDGE_TYPE_MAX_VALUE` — which the agent
bridge's `edgeKindNames` and `getTypeForReadableTypeString` iterate up to, so `graph_trail
{edge_kinds: ["dispatch"]}` works), `GraphViewStyle` (call offsets, dashed), the seven color
schemes (`graph/edge/dispatch`, the scheme's call color), the legend ("possible dispatch"),
`QtGraphEdge`'s tooltip, the custom-trail dialog's edge filters, the default trail of a
callable (`call | dispatch | override`, so a callee trail continues into the overrides and a
caller trail from an override reaches the calls through its base), `PersistentStorage::
getNodeIdsForLocationIds` (below), `LadybugGraphStorage`'s per-kind tables, and the bridge's
catalog text. FlatBuffers carry `edge_type` as an `int32`, so neither schema changes; the
indexers never produce the kind, so their mirrors of the enum (`mod.rs`, `StorageKinds.swift`,
`storage.zig`) do not list it.

No storage version bump. An index written before this has no dispatch edges and is read
correctly; it gains them on its next refresh, which derives over the whole index whatever it
reindexed. A bump would force every existing project into a full re-index for an additive
relation.

### The derived edge carries the call's occurrences

The pass copies the occurrences of the CALL edges an edge was derived from onto it. So
activating the dispatch edge shows the call sites, and activating an override lists the
virtual calls that may reach it among its references — "where can this implementation be
entered from" is the question the feature exists to answer. Clicking the call site in the
code still goes to the method it names: `getNodeIdsForLocationIds` treats a dispatch edge as
it treats an override edge, as a fallback used only when nothing else is at the location.

### Where it runs, and why recompute-everything is the refresh story

`TaskFinishParsing::report`, after the storage is switched to read mode (which rebuilds the
`edge(source_node_id)`, `edge(target_node_id)` and `occurrence(element_id)` indices the
recursion and joins use) and before the database is optimized and handed to the decision on
the run's data, so it runs before the temp DB is swapped in. Every run ends there: full,
incremental, interrupted, shard. The `merge` command derives again over the merged index,
since each shard only saw its stripe.

Incremental correctness is the simplest kind: the pass deletes every dispatch edge and
derives them all again. It also runs first thing in `clearFileElements`, for a reason that
is easy to miss: the clear path keeps a node without occurrences alive while it is still the
end of a non-member edge, so a dispatch edge into an override that was declared only in a
cleared file would have kept it (and itself) in the index. The refresh test covers exactly
that case.

Cost on bgfx (4,682 files, 1.5M edges, of which 300k calls and 2.5k overrides): see
"Measured on bgfx". It is a fraction of a second against minutes of indexing, so
maintaining the relation incrementally (Differential Dataflow, the roadmap's phase 3) is not
worth anything yet.

### Semantics

- **The static target is the CALL edge's target**: what the indexer resolved the call to.
  For C++ that is the method name lookup found in the static type of the object expression,
  so `d->f()` on a `Derived*` starts from `Derived::f` when Derived declares one.
- **The closure is over OVERRIDE edges, unbounded.** `overrider(base, impl)` is the
  transitive closure of "impl overrides base" (an OVERRIDE edge points overriding ->
  overridden, one level), seeded with called methods only. `UNION` rather than `UNION ALL`:
  a diamond (`D::f` overrides `B::f` and `C::f`, both overriding `A::f`) reaches `D::f` twice
  and yields one edge, and a cycle, which only a serialized-name collision can make,
  terminates.
- **INHERITANCE is not consulted.** The roadmap's rule joins `subtype` and `method_of`; on
  bgfx that filter only ever *removed* real overrides (52 edges): `bx::FileWriter::write`
  overrides `bx::WriterI::write`, but bx's headers are outside the indexed paths, so the class
  declaration and its INHERITANCE edge are not recorded while the override, recorded from the
  out-of-line definition in an indexed source file, is. An override already implies the
  subtype relation; the inheritance edge is the less complete record of it.
- **Through abstract declarations:** the recursion walks every override, so a pure virtual
  re-declaration in the middle of a chain does not stop it, and the chain's lower overrides
  are reached. Edges are also derived to such intermediate pure virtual overrides: the index
  does not record "pure", so the pass cannot leave them out. They are never the target a
  call actually runs, which is a known imprecision, and a harmless one to look at.
- **Not derived:** an edge to the method the call names (that is the CALL), an edge the
  caller already has as a direct CALL, and anything from a call whose caller is itself an
  override of the method it calls (next section).

### The super-call rule, and what the edges do not say

A call from an override to the method it overrides is taken to be a call of the base
implementation, and derives nothing. In C++ it is `Base::f()` inside `Derived::f`, and
— much more often — the implicit call of the base class destructor at the end of every
derived destructor, which `CxxDestructorCallRecorder` records as a CALL. Swift's
`super.f()` is the same shape. None of these dispatch. Without the rule, every subclass's
method gets edges to all its siblings' overrides: on bgfx about 970 such calls made 26,099 of
42,827 derived edges (61%), some 23,000 of them from destructor chains alone.

The rule is a heuristic, and it has a price: a composite or decorator that forwards to
another object of its own base type — `TIntermAggregate::traverse` calling
`child->traverse()`, `HashWriter::write` calling `m_writer->write()` — does dispatch, and
loses its derived edges (its CALL to the base method stays).

**This is the answer to the roadmap's open question "do the base edges carry enough
information?": for C++, no.** A CALL edge does not say whether the call it records was
dispatched. Clang knows it exactly — `MemberExpr::performsVirtualDispatch()` is false for a
qualified call, the CFG's implicit destructors are never virtual, a call on an object (not a
pointer or reference) of a class type is bound statically, `delete p` dispatches when the
destructor is virtual — but the indexer does not record it. The fact that would remove both
the false edges and the heuristic is a per-call-edge "dispatches" bit, set if any call site
behind the edge dispatches (OR is what survives merging the same edge from many translation
units). It needs a carrier through `ParserClient`, the intermediate storage and its
FlatBuffers schema, which `element_component` does not have today; that is the next step,
not this one. The roadmap's other missing relation, the receiver's static type
(`virtual_call(Call, DeclM, RecvT)`), matters less: the static target already encodes it
whenever the receiver's class redeclares the method.

### Per language

The pass is language-neutral: it reads CALL and OVERRIDE edges, whoever wrote them.

- **C++:** OVERRIDE from `CXXMethodDecl::overridden_methods` (direct overrides, implicit
  destructors included), CALL from the reference indexer. Verified end to end (tests, bgfx).
- **Rust:** the indexer records `impl` method -> trait method as OVERRIDE
  (`resolve_impl_overrides`), and a call through `dyn Trait` or a generic bound as a CALL to
  the trait method, so trait dispatch is covered. Not verified on a real crate in this change.
  A trait's default method that an impl does not override has no OVERRIDE edge, correctly:
  the call runs the default, which is the CALL's own target.
- **Swift:** OVERRIDE comes from the index store's `overrideOf` relation, which covers class
  overrides and protocol requirement witnesses. Not verified on a real package in this
  change. The index store also has a `dynamic` role on call occurrences, the fact C++ lacks;
  the Swift indexer does not record it yet.
- **Zig:** no inheritance, no OVERRIDE edges, nothing to derive.

## Measured on bgfx

`C/C++ from Makefile`, `make osx-arm64-debug` (bgfx, bx, bimg and the shader tools' third
party code: glslang, spirv-tools, spirv-cross, tint), 907 translation units, 1,857 files,
333k nodes, 1.53M edges (300,659 CALL, 2,532 OVERRIDE). Full index 2 min 35 s.

| | edges | from calls | time |
|---|---|---|---|
| derived, as shipped | **16,728** (23,559 occurrences) | 2,700 call edges, 1,672 callers | **307 ms** in the run (152 ms for the recursive query in `sqlite3`) |
| without the super-call rule | 42,827 | | |
| without it, and with the roadmap's subtype filter | 42,775 | | 452 ms (query) |

Removing them (the first statement of a refresh's clear) takes 0.2 s. A refresh of an
unchanged project does not run the pass ("Nothing to refresh"); one that indexes anything
runs it over everything, for the same 0.3 s.

Through the renderer interface: calls of `bgfx::RendererContextI` methods gain 200 edges, to
the four implementations compiled on macOS — `mtl::RendererContextMtl` (49),
`vk::RendererContextVK` (54), `wgpu::RendererContextWGPU` (49) and `noop::RendererContextNOOP`
(48); the GL renderer is not part of this make target. `bgfx::Context::rendererExecCommands`
alone gets 139 (every `createShader`, `destroyProgram`, ... it forwards), and
`bgfx::Context::renderFrame -> mtl::RendererContextMtl::submit` is the edge from the frame
loop into the Metal renderer that the graph could not show before.

A refresh (a small compilation-database project: two files changed, one override moved from
one class to another) leaves exactly the nodes and edges a full index of the changed sources
has.

## Not done

- The "dispatches" bit on CALL edges (above), which would retire the super-call heuristic.
- Excluding pure virtual intermediates (needs a modifier the C++ indexer does not emit).
- Turso compare mode (`SOURCETRAIL_TURSO_COMPARE`, off by default) mirrors the pass's raw
  statements to Turso like any other; whether turso_core accepts the recursive CTE was not
  tried. If it does not, the mirror reports a divergence for that statement and SQLite, the
  source of truth, is unaffected. The concurrent mode is unaffected: the pass runs after the
  Turso ingest has been exported to SQLite.
- The custom-trail dialog does not list `binds` either; that predates this change.
