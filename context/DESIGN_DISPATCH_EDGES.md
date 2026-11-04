# Design: dispatch edges — class-hierarchy analysis as a pass over the index

- **Status:** shipped (2026-11). The first derived relation in the index, and the smaller
  first cut of "dispatch resolution via CHA" in
  [ROADMAP_ANALYSIS_ENGINES.md](ROADMAP_ANALYSIS_ENGINES.md) (Tier 2): no Datalog engine,
  one SQL pass (`WITH RECURSIVE`) over the stored edges after indexing.
- **Code:** `SqliteIndexStorage::deriveDispatchEdges` / `removeDispatchEdges`
  (`src/lib_core/data/storage/sqlite/SqliteIndexStorage.cpp`), called from
  `TaskFinishParsing::report`, `PersistentStorage::clearFileElements` and the `merge`
  command; what a C++ call does, `utility::callDispatches` (`src/lib_cxx/data/parser/cxx/
  utilityClang.inl`). Tests: `StorageTestSuite.cpp` ("storage derives ...", "storage refresh
  ...", "cxx indexer records whether a call dispatches ..."), `IpcSerializerTestSuite.cpp`.
- **Related:** [DESIGN_STATIC_DISPATCH_EDGES.md](DESIGN_STATIC_DISPATCH_EDGES.md) (the
  compile-time counterpart, a second pass run right after this one);
  [DESIGN_STORAGE_SCHEMA.md](DESIGN_STORAGE_SCHEMA.md) (the tables and the
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
  caller already has as a direct CALL, anything from a C++ call that does not dispatch, and,
  for a call no indexer said anything about, anything from a caller that is itself an
  override of the method it calls (next section).

### Which calls dispatch: the C++ indexer says, from clang

The first cut had to guess, because a CALL edge did not say whether its call dispatched; that
was the answer to the roadmap's open question "do the base edges carry enough information?"
(for C++, no). Now the C++ indexer records it, for every call of a virtual method, and the
pass derives edges from exactly the calls that dispatch.

**What decides it** (`utility::callDispatches`, `utilityClang.inl`) is what clang's code
generation decides when it emits the call — `CodeGenFunction::EmitCXXMemberOrOperatorMemberCallExpr`
and `EmitObjectDelete` in `clang/lib/CodeGen/CGExprCXX.cpp` — read off the same AST:

- **A member call** (`p->f()`, `r.f()`, `f()` on an implicit `this`; `visitMemberExpr`):
  static if `MemberExpr::performsVirtualDispatch()` is false (a qualified `Base::f()`,
  [class.virtual]/16), or if `CXXMethodDecl::getDevirtualizedMethod(object)` finds the target
  at compile time (`clang/lib/AST/DeclCXX.cpp`): a `final` method or class, a call on an object
  rather than through a pointer or reference — a variable or member of class type, a class
  prvalue — whose dynamic type is its static one.
- **On `this` during construction or destruction:** in a constructor, a destructor or a
  default member initializer, the object is of the class being built or torn down
  ([class.cdtor]/4), so a call on `this` does not reach a derived class's override. Clang's
  code generation leaves this to the vtable pointer, so this rule is ours; the test is that
  `getBestDynamicClassTypeExpr()` of the object is a `CXXThisExpr`.
- **A member operator** with operator syntax (`a == b`): its object is the first operand, so
  the visitor now hands the reference indexer the enclosing `CXXOperatorCallExpr`
  (`beginTraverseCXXOperatorCall`); the same rules apply.
- **`delete p`** (`visitCXXDeleteExpr`): dispatches when the destructor is virtual and
  `getDevirtualizedMethod` cannot bind it, as in `EmitObjectDelete`.
- **Implicit destructor calls** from the CFG (`CxxDestructorCallRecorder`) — of base and
  member subobjects, automatic objects, temporaries — never dispatch. A `CFGDeleteDtor` is
  left to the delete expression, which records the same call with its answer.
- Nothing is recorded for a method that is not virtual (there is nothing to dispatch to), nor
  where the object's type depends on a template parameter (the uninstantiated pattern; the
  instantiations are judged).

**How it travels.** `ParserClient::recordCallDispatch(edgeId, dispatches)` puts an
`element_component` on the CALL edge: `ELEMENT_COMPONENT_CALL_DISPATCHES` (2) or
`ELEMENT_COMPONENT_CALL_STATIC` (3). `IntermediateStorage` and `Storage::inject` already
carried element components (with their ids remapped, through `TaskMergeStorages`, the
chunker, `merge`, the Turso writer and export); what was missing was the IPC wire, where
`IntermediateStorage` gains an appended, optional `element_components` vector
(`abi-schemas/ipc-indexer/intermediate_storage.fbs`). The Rust, Swift and Zig indexers do not
write it. Swift and Zig needed no change; Rust's two object-API literals take
`..Default::default()` (they listed every field, so any appended field breaks them).

**How it combines.** An edge is the union of a caller's calls of a method, recorded by every
translation unit that sees them. The facts are kept once per edge and kind
(`SqliteIndexStorage::addElementComponent`, with an in-memory set seeded from the database),
and the pass reads them as an OR: the edge dispatches if any recorded call does, is static if
only static calls were recorded, and is unknown if no fact was. Merging shards is the same
inject, so the same OR. On a refresh the components go with their edge: a cleared file takes
the call edges only it recorded, facts included, and the reindexed file records them again. An
edge that survives a clear because another, kept, file also records it keeps its facts as
well, which can leave a stale "dispatches" — an over-approximation, and in practice not
reached, since all of a caller's calls are in the file that defines the caller.

**Storage.** No new table or column, so no storage version bump: `element_component` exists
in every index. An index written before this has no facts, which reads as "unknown"
everywhere and gives the first cut's result until the next run.

**The fallback for calls without a fact** — other languages, an older index — is the first cut's guess: a call from an override to a
method it overrides is taken for a call of the base implementation (`Base::f()`, `super.f()`,
the implicit base destructor call) and derives nothing. It is wrong for a composite or
decorator forwarding to another object of its base type, which is why C++ no longer relies on
it. The roadmap's other missing relation, the receiver's static type
(`virtual_call(Call, DeclM, RecvT)`), is still not recorded and matters less: the static target
already encodes it whenever the receiver's class redeclares the method.

### Per language

The pass is language-neutral: it reads CALL and OVERRIDE edges, whoever wrote them.

- **C++:** OVERRIDE from `CXXMethodDecl::overridden_methods` (direct overrides, implicit
  destructors included), CALL from the reference indexer, with whether each call dispatches.
  Verified end to end (tests, bgfx).
- **Rust:** the indexer records `impl` method -> trait method as OVERRIDE
  (`resolve_impl_overrides`), and a call through `dyn Trait` or a generic bound as a CALL to
  the trait method, so trait dispatch is covered, with the fallback rule (it records no
  dispatch fact). Not verified on a real crate in this change.
  A trait's default method that an impl does not override has no OVERRIDE edge, correctly:
  the call runs the default, which is the CALL's own target.
- **Swift:** OVERRIDE comes from the index store's `overrideOf` relation, which covers class
  overrides and protocol requirement witnesses. Not verified on a real package in this
  change. The index store also has a `dynamic` role on call occurrences, the fact the C++
  indexer now records; the Swift indexer does not record it yet, so Swift calls take the
  fallback.
- **Zig:** no inheritance, no OVERRIDE edges, nothing to derive.

## Measured on bgfx

`C/C++ from Makefile`, `make osx-arm64-debug` (bgfx, bx, bimg and the shader tools' third
party code: glslang, spirv-tools, spirv-cross, tint), 907 translation units, 1,857 files,
333k nodes, 1.53M edges (300,659 CALL, 2,532 OVERRIDE). Full index 2 min 35 s, with or
without recording the facts.

| | edges | callers | time |
|---|---|---|---|
| **with the C++ dispatch facts** (current) | **19,295** (26,369 occurrences) | 1,679 | **315 ms** in the run |
| first cut: the super-call guess for every call | 16,728 (23,559 occurrences) | 1,672 | 307 ms (152 ms for the recursive query in `sqlite3`) |
| no rule at all | 42,827 | | |
| no rule, and the roadmap's subtype filter | 42,775 | | 452 ms (query) |

The facts: 8,743 (6,416 "dispatches", 2,327 "static"), one per call edge and kind, 29 edges
carrying both (a caller that calls the same method both ways). Of the 3,689 call edges to an
overridden method, 3 have no fact and take the guess: all three are in uninstantiated class
templates (`HashWriter<Ty>::write`, `Scalar<T>::Clone`, `OperandInstruction<N, R>::Destroy`),
where `this` is type-dependent; their instantiations are judged.

Against the first cut, the facts add 2,666 edges and remove 99:

- **Gained, 2,666 edges from 82 callers, every one of them an override forwarding to another
  object of its base type** — the case the guess threw away. `glslang::TIntermAggregate::
  traverse` (11 edges before, 22 now: its children's `traverse`), `bimg::HashWriter<
  bx::HashAdler32>::write` (0 before, 9 now: `m_writer->write()` reaches `bx::FileWriter`,
  `bx::DebugWriter`, `bgfx::Bin2cWriter`, ...), and tint's type and constant nodes, whose
  `FriendlyName` and `Clone` recurse into their element types (42 edges each, e.g.
  `tint::core::type::Vector::FriendlyName`). Each of the 82 also gains the edge to itself (a
  vector of vectors).
- **Removed, 99 edges from 49 callers, every one a call clang binds at compile time:** calls
  on local objects (`bgfx::glsl::compileSpirvCross`'s `spirv_cross::CompilerGLSL compiler;
  compiler.compile()` no longer reaches `CompilerMSL`, `CompilerHLSL`, `CompilerCPP` ...), 58
  to destructors of local objects (`tint::UniqueAllocator<...>::Get`'s `prototype`,
  `glslang::TProgram::mapIO`'s `TIoMapper`), qualified calls (`spvtools::opt::SERecurrentNode::
  AddCoefficient`'s `SENode::AddChild(child)`), calls through a pointer to a `final` class
  (tint's `let->Destroy()`, `Let` being final: it can only run `OperandInstruction<1, 1>::
  Destroy`), and calls on `this` in constructors (`glslang::TVariable`'s copy constructor
  calling `setExtensions`).

Removing them (the first statement of a refresh's clear) takes 0.2 s. A refresh of an
unchanged project does not run the pass ("Nothing to refresh"); one that indexes anything
runs it over everything, for the same 0.3 s.

Through the renderer interface (the same with and without the facts: `m_renderCtx` is a
pointer): calls of `bgfx::RendererContextI` methods gain 200 edges, to
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

- Dispatch facts from the Rust and Swift indexers (Swift's index store has the `dynamic`
  role), which would retire the fallback rule for them too.
- The derived edge still copies all occurrences of the call edge it comes from, the
  non-dispatching calls of a caller that also has dispatching ones of the same method
  included: the fact is per edge, not per call site.
- In a constructor of C, `static_cast<Base*>(this)->f()` runs C's override of `f`, not
  `Base::f`; it is recorded as static, and the edge to C's override is not derived.
- Excluding pure virtual intermediates (needs a modifier the C++ indexer does not emit).
- Turso compare mode (`SOURCETRAIL_TURSO_COMPARE`, off by default) mirrors the pass's raw
  statements to Turso like any other; whether turso_core accepts the recursive CTE was not
  tried. If it does not, the mirror reports a divergence for that statement and SQLite, the
  source of truth, is unaffected. The concurrent mode is unaffected: the pass runs after the
  Turso ingest has been exported to SQLite.
- The custom-trail dialog does not list `binds` either; that predates this change.
