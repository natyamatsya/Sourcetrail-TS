# Design: static dispatch edges — what a template calls through its arguments

- **Status:** shipped (2026-11). The second derived relation in the index, next to the dispatch
  edges, and the build of "Part 2" of
  [DESIGN_CONCEPT_SATISFACTION.md](DESIGN_CONCEPT_SATISFACTION.md#part-2--static-dispatch-through-template-instantiations-investigated).
- **Code:** `SqliteIndexStorage::deriveStaticDispatchEdges` / `removeStaticDispatchEdges`
  (`src/lib_core/data/storage/sqlite/SqliteIndexStorage.cpp`), called from
  `TaskFinishParsing::report`, `PersistentStorage::clearFileElements` and the `merge` command, as
  the dispatch pass is; the instantiations reach the graph in `PersistentStorage::
  addEdgeComponentsToGraph` (`TokenComponentInstantiations`) and the tooltip in `QtGraphEdge::
  coFocusIn`. Tests: `StorageTestSuite.cpp` ("storage derives static dispatch edges ...",
  "storage refresh derives the static dispatch edges ...", "storage derives the same static
  dispatch edges ...", "graph of a template names the instantiations ..."),
  `GraphControlsTestSuite.cpp`.
- **Related:** [DESIGN_DISPATCH_EDGES.md](DESIGN_DISPATCH_EDGES.md) — the run-time counterpart,
  whose contract (derived, recomputed from scratch, removed before a clear) and checklist of
  edge-kind enumerations this follows.

## What it adds

```cpp
template <Shape T> void render(T& t) { t.draw(); helper(t); }
template <typename T> void helper(T& t) { t.draw(); }
template <typename T> struct Painter { T shape; void paint() { shape.draw(); } };
```

The graph of `render` showed no edge to any `draw`: the call depends on `T`, so in the template
it resolves to nothing, and the C++ indexer records a call only where clang resolved it — in the
implicit instantiations, `render<Circle> -> Circle::draw`, located at the template's own `t.draw()`.
Those instantiation nodes are implicit and hidden behind the template's specialization edges.
Now the template has an `EDGE_STATIC_DISPATCH` edge to every function its instantiations call
through their arguments: `render -> Circle::draw`, `render -> Square::draw`, `Painter<T>::paint ->
Circle::draw`. Drawn as a call, dashed, in the color of template specializations (the dispatch
edge is the call color, dashed); its tooltip says "static dispatch through a template argument",
names the instantiations ("via render<Circle>") and counts them.

No indexer change: the facts were in the index (see Part 2 of the concept note). A pure SQL pass
after indexing, like the dispatch pass, so it works for any indexer that writes implicit
specializations with TEMPLATE_SPECIALIZATION and CALL edges (today: C++).

## Decisions

### A new kind, `EDGE_STATIC_DISPATCH = 1 << 16`, "static dispatch"

For the reasons the dispatch edge is one (DESIGN_DISPATCH_EDGES.md, "A new edge kind"): a CALL is
what the source says, the derived relation is removable by type, and the dedup key never merges a
derived edge into a written one. Template -> callee, horizontal like a call. The enumeration
checklist, all handled: `Edge.h/.inl` (enum, `intToEnum`, readable name, `EDGE_TYPE_MAX_VALUE` —
so the bridge's `graph_trail {edge_kinds: ["static dispatch"]}` works), `GraphViewStyle` (call
offsets, dashed), the seven color schemes and the template (`graph/edge/static_dispatch`, each
scheme's template-specialization color), the legend ("static dispatch": a template, its
instantiation, the instantiation's call and the derived edge), the tooltip, the custom-trail
dialog's edge filters, the callable's default trail (`call | dispatch | static dispatch |
override`: a callee trail from a template goes on into what its instantiations call, a caller
trail from `Circle::draw` reaches the templates that call it through `T`), the location-click
rule (below), the Ladybug table (`StaticDispatch`) and the bridge's catalog. No bundle of its own
(it bundles as calls do), no `ReferenceKind` (no indexer records it), no IPC or schema change, no
storage version bump: an older index gains the edges on its next run.

### Which instantiation: kept with the edge

The edge joins `render` and `Circle::draw`; *which* instantiation made the call is the
information that `render<Circle>`'s node carried and the edge would otherwise lose. The pass
writes one `element_component` per (edge, instantiation), kind
`ELEMENT_COMPONENT_STATIC_DISPATCH_VIA` (4), `data` the specialization's node id, as the dispatch
facts use the same table. The graph loads the components of its edges with the ambiguity flags
(one query, already made), resolves the names (`render<Circle>`, `Painter<Circle>::paint`,
`pair<Circle, int>`) and the tooltip lists up to four, cut at 100 characters, "and N more", with
the count ("3 instantiations"). The cheaper option — deriving the name from the edge's call sites
in the code view — does not work: every instantiation's call shares the template's location, so
the location says nothing about the argument. On bgfx: 17,847 components for 4,420 edges (at most
415 on one edge, a `tint::Switch` dispatch target). They are deleted with their edge by the
foreign key's cascade; a merged shard's components name the shard's node ids, which is harmless,
since the merge derives everything again and the stale ones go with their edges.

The bridge exposes the edge like any other (`edge_type` 65536 in `graph.edges`); the
instantiation names are in the tooltip only, not in the bridge's state.

### What counts: dependent calls, explicitly defined targets

From each implicit specialization (`symbol.definition_kind = IMPLICIT` with a
`TEMPLATE_SPECIALIZATION` edge to its template), its CALL edges, keeping:

- **Not what the template calls itself.** A non-dependent call (`log()` in the body) is recorded
  on the template and on every instantiation; it is the template's CALL already. Also a call to an
  instantiation of a template the template calls itself: in a class template, `operator[]`'s call
  of `allocBlock(...)` is resolved in the template to `FrameArenaT<Ty, BlockT>::allocBlock` and in
  the instantiation to `FrameArenaT<Rect, 64>::allocBlock`; it is not dependent, and what
  `allocBlock` reaches is `allocBlock`'s own static dispatch, not `operator[]`'s.
- **Leaves: only explicitly defined targets** (`definition_kind = EXPLICIT`), which leaves out
  implicit specializations (a container's internals calling each other: most of the 94k calls the
  concept note counted), implicitly declared special members (an element type's implicit copy
  constructor) and everything outside the indexed files (no symbol row: `std::forward<X>`,
  `std::span<X>::...` — instantiations of library templates that are not marked implicit, because
  they have no symbol at all). An explicit specialization chosen by the argument
  (`vk::getType<VkBuffer>`, `template <> ...`) is explicitly defined, and kept: picking it is
  static dispatch.
- **Hops: calls to other implicit specializations** are followed, recursively: `render<Circle> ->
  helper<Circle> -> Circle::draw` gives `render -> Circle::draw`. Collapsing is needed because the
  primary records no call for `helper(t)` either (dependent: the C++ indexer records nothing for
  an unresolved call), so without it `render` would show nothing of what `helper` does for it. The
  recursion is walked backwards from the specializations that have leaves, `UNION`, so a cycle
  (`ping<T>` and `pong<T>` calling each other, `f<N>` calling `f<N - 1>`) ends.
- **Sources: explicitly defined templates.** A function template, a member function template, a
  member of a class template (`Painter<T>::paint`, which has its own TEMPLATE_SPECIALIZATION edge
  from `Painter<Circle>::paint`), a member template of a class template (`Painter<T>::with<U>`).
  A member template of a class template's *implicit* specialization (`HashmapBase<X, 1>::Node::
  Equals<T>`) is a template node of its own that is implicit and has no edge back to the one in
  the source: left out (766 such templates on bgfx, all in tint).
- **Not derived:** an edge from a template to itself, or one the template has as a direct CALL.

Measured against the alternatives on bgfx: without the explicit-target rule (only "not an
implicit specialization", the concept note's suggestion) 72,506 pairs, two thirds of them to
library internals without a symbol row; without collapsing 2,119 pairs; with collapsing through
every hop 4,614; with hops through the template's own calls cut, as shipped, 4,415 in the
prototype and 4,420 in the run.

### Location, and clicks

The edge occurs where the calls in the template's body that lead to the callee do: the dependent
call itself (`t.draw()`), or, through a helper, the call of the helper (`helper(t)`) — always in
the source template's body, as a call edge's occurrences are in its caller's. Activating the
edge shows them; `Circle::draw`'s references list `render`'s `helper(t)` among them.

Clicking a call site does **not** use the edge, not even as the fallback the dispatch edge is.
At `t.draw()` the instantiations' calls already lead to `Circle::draw` and `Square::draw`; at
`helper(t)` they lead to `helper<Circle>` and `helper<Square>` (implicit nodes, themselves the
fallback), and a fallback static dispatch edge would add `Circle::draw` and `Square::draw` to that
list, the helper's callees in place of the helper. `getNodeIdsForLocationIds` skips the kind.

### Where it runs, and refresh

Right after the dispatch pass, in the same places: `TaskFinishParsing::report` once read mode has
rebuilt the edge indices, `merge` over the merged shards, and `clearFileElements` removes the edges
first so that they never keep a node alive. Every run recomputes all of them from scratch, whatever
it reindexed; the refresh test compares a refresh (an instantiation removed from `use.cc`) with a
full index of the changed files, edges, occurrences and instantiations included.

One fix came with it: `element_component(element_id)` was indexed in `CLEAR` mode only. The passes
delete their old edges in `READ` mode (on a run without a clear: a refresh that only adds files,
`merge`, a second derive), and every deleted element's cascade then scanned the whole component
table: removing bgfx's 19,295 dispatch edges took 30 s once the static dispatch components were in
it (3.8 s for the 4,420 static dispatch edges). The index is now also built for `READ` (0.26 s and
0.12 s); the graph's lookup of an edge's components by element id uses it too.

### Cost

SQLite has no statistics on this database (no `ANALYZE`), so the plan is the statement's shape.
Two shapes cost a lot and were changed: inserting the recursion into a `WITHOUT ROWID` table (37 s,
against 30 ms into a plain table), and leaves and hops in one table, where the join on the leaves
got an automatic index on `is_hop` (5 s, against 0.1 s with two tables). What is left: one scan of
the edges for the calls and specialization edges of implicit symbols (0.17 s, there is no index on
the type), the instantiations' calls classified against the templates' own (0.2 s), the recursion
(30 ms), the derivation (0.1 s), and the writes.

## Measured on bgfx

The project of DESIGN_DISPATCH_EDGES.md (`make osx-arm64-debug`, 907 TUs, 1.53M edges), indexed
fresh with this change: full index 2 min 59 s; **4,420 static dispatch edges in 669 ms** (the
dispatch pass: 19,295 edges in 417 ms), 5,658 occurrences, 17,847 instantiation components.
685 templates have edges, to 1,778 callees.

- **bgfx, bx and bimg's own templates: 275 edges from 52 templates**, among them the CRTP renderer
  bases the concept note named: `ChunkedScratchBufferT<Derived, BufferTy, ChunkTy>::end ->
  {mtl,vk,wgpu}::ChunkedScratchBuffer{Mtl,VK,WGPU}::flushChunk` and `currentFrameInFlight`, `::addChunk
  -> ::createChunk`, `::destroy -> ::destroyChunk`; `TimerResultT<ResultT>::reset -> {mtl,vk,wgpu}::
  TimerQuery*::Result::reset`; `faultInFromCache<...> -> vk::VideoDecoderVK::decode` and
  `mtl::VideoDecoderMtl::decode`; `HandleArenaT<Ty, ...>::alloc -> mtl::ShaderMtl::ShaderMtl`,
  `vk::TextureVK::TextureVK`, ... (the constructors of every resource type); `vk::RendererContextVK::
  release<Ty> -> vk::getType<VkBuffer>`, ...; through a helper, `StateCacheT<Ty>::add -> vk::release(
  VkRenderPass&)`, ... (via `StateCacheFuncT<Ty>::evict`); 31 of the 275 come only through hops.
- **Everything else is tint, glslang, spirv-tools and spirv-cross**, where the large ones are
  factories and visitors: `tint::Switch<...>` (207 callees), `BlockAllocator<T>::Create<TYPE,
  ARGS...>` (198 constructors), `tint::core::ir::Builder::InsertBefore<FUNCTION>` (167),
  `spvtools::MakeUnique<T, Args...>` (157). Out-degree: 219 templates with one edge, 395 with 2–10,
  65 with 11–100, 6 with more. 2,298 of the 4,420 edges come only through hops.

**Noise.** 3,249 edges go to a target that only some of the template's instantiations reach — the
edges the feature is for. 815 go to a target every one of at least two instantiations reaches:
mostly calls whose arguments are dependent but whose target is not (`FrameArenaT<Ty>::create ->
bx::memSet(ptr, 0, sizeof(Ty) * n)`, `TimerResultT<ResultT>::grow -> bx::memCopy`), or members
constructed in a class template's constructor (`FrameArenaT::FrameArenaT -> bx::Mutex::Mutex`).
The template has no CALL edge to those either (clang leaves a call with a dependent argument
unresolved in the template), so the static dispatch edge is the only place the graph shows them;
they are kept. Activating a bgfx template shows what the reviewer wants to see: `ChunkedScratchBufferT
::end` gets its six edges into the three backends next to its own calls; the tint factories are
long lists, bundled as long call lists are.

## Not done

- The instantiation names are not in the bridge's state; an agent sees the edge, not "via".
- The template's own unresolved calls (`render`'s `helper(t)`) still have no edge to `helper`:
  the collapsed edge stands for what the helper does, not for the call of the helper. A derived
  "template calls template" edge (from the hop's callee's template) would be the next step.
- Member templates of implicit class template specializations (766 on bgfx) are left out; mapping
  them to the template in the source would need the member's parent's specialization edge and a
  name match.
- Function-local classes and lambdas instantiated in a template: their calls are recorded on
  their own nodes, which are not specializations; what a generic lambda passed to a template
  calls is reached only if the lambda's operator() instantiation has a specialization edge.
- Calls inside non-indexed (library) templates are not recorded at all (no traversal), so a
  dispatch through `std::visit` or `std::invoke` is not seen, as in the concept note.
- Turso compare mode mirrors the raw statements as for the dispatch pass; not tried.
