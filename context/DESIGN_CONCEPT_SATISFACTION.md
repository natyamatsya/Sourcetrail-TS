# Design: concept satisfaction edges — compile-time polymorphism in the graph

- **Status:** Part 1 shipped (2026-11): `EDGE_SATISFIES`, type -> concept, recorded by the C++
  indexer. Part 2 (static dispatch through template instantiations) investigated, not built.
- **Code:** `CxxConceptReferenceRecorder::recordSatisfiedConstraints` /
  `recordSatisfiedConceptSpecialization` (`src/lib_cxx/data/parser/cxx/
  CxxConceptReferenceRecorder.inl`), called from the reference indexer (calls, variable
  templates, constructions, concept-ids) and the type indexer (class and alias template
  specializations). Tests: `CxxParserTestSuite.cpp` ("cxx parser records ... concept ..."),
  `StorageTestSuite.cpp` ("storage refresh keeps a satisfies edge ..."),
  `GraphControlsTestSuite.cpp`.
- **Related:** [DESIGN_DISPATCH_EDGES.md](DESIGN_DISPATCH_EDGES.md) (the run-time counterpart,
  and the same checklist for a new edge kind).

## Part 1 — what a satisfies edge says

Inheritance shows run-time polymorphism: activate a base class and its derived classes hang
below it. A C++20 concept is the compile-time interface, and the graph showed nothing of who
implements it — a constrained template got a USAGE edge to the concept, and that was all. Now a
type has an `EDGE_SATISFIES` edge to each concept the code proves it satisfies. Activating
`PresentsQuestions` shows `QtDialogView`, `Dialogs`, `WindowWithDialogs` and the test fakes
below it, the way a base shows its derived classes; its trail is the same tree.

### Where the proof comes from

1. **A specialization of a constrained template.** It exists only because its arguments
   satisfied the template's constraints, so each concept-id in their conjunction holds for the
   argument it constrains. The constraints are clang's own list,
   `TemplateDecl::getAssociatedConstraints` — the type constraints of the parameters
   (`template <Shape T>`, the invented parameter of an abbreviated `Shape auto`), the
   requires-clause, a function's trailing requires-clause — in terms of the primary template's
   parameters, matched against the specialization's converted arguments by depth and index.
   - `A && B`: both. `A || B`, `!A`, a requires-expression, a type trait: nothing (it does not
     say which concept holds, or names none).
   - A constrained pack `template <Shape... Ts>` is the fold `(Shape<Ts> && ...)`: every
     element.
   - Only a constraint whose first argument *is* a template parameter is matched;
     `Shape<T*>` or `Shape<typename T::value_type>` would take substituting.
   - Which specializations: function templates (free, member, constructor templates) at their
     calls and references; class templates where a type names them (`Box<Circle>`) or a
     construction deduces them (`Box b{circle}`); alias templates with their written arguments;
     variable templates at their references.
2. **A concept-id clang evaluated** (`ConceptSpecializationExpr::isSatisfied`):
   `static_assert(Shape<Circle>)`, an `if constexpr (Shape<T>)` in an instantiation.
   Recorded only when it holds; a dependent one, in a template's definition, is not evaluated.

### Decisions

- **Edge kind, direction, layout.** `Edge::EDGE_SATISFIES = 1 << 15`, readable `satisfies`,
  type -> concept like derived -> base, and in `LAYOUT_VERTICAL`, so the graph places satisfying
  types below a concept. Drawn as UML's realization: inheritance's hollow triangle on a dashed
  line, in the color of template specializations (the compile-time family). Tooltip "satisfies
  concept". Same enumeration checklist as the dispatch edge: `Edge.h/.inl`, `ReferenceKind`
  (`SATISFIES = 13`; `intToEnum` also gained the missing `BINDS`), `ParserClientImpl`'s mapping,
  the seven color schemes and the template, the legend ("concept satisfaction"), the custom
  trail's edge filters, the trail buttons (a concept: "show satisfying types", top down), the
  default trails (a concept: `satisfies`; a class: `inheritance | satisfies | template
  specialization`, so a class's base hierarchy includes the concepts it satisfies), two bundles
  ("Satisfying Types", "Satisfied Concepts") next to the inheritance ones, location clicks
  (`getNodeIdsForLocationIds` treats the edge as a fallback, as override and dispatch edges:
  clicking `render` in `render(circle)` goes to `render`), the Ladybug table, and the bridge's
  catalog and edge-kind names. The IPC carries the edge as any other: no schema change.
- **Which types.** A class, struct, union or enum (any `TagDecl`, class template
  specializations included), or a built-in type, after dropping references and cv-qualifiers
  (a forwarding reference deduces `Circle&`; the node is `Circle`'s). A pointer, array or
  function type has no node of its own and is left out rather than drawn as what it points to.
- **Multi-parameter concepts:** the first argument satisfies, the rest are the concept's
  arguments: `template <std::convertible_to<int> T>` records `T -> convertible_to`.
- **Noise.** Recorded when the concept or the type is declared in a project (indexed) file. So
  `Circle -> std::copyable` from the project's own call of a constrained library function is
  recorded, `int -> std::integral` is not, `int -> Small` (the project's concept) is. Of a
  library's concepts two kinds are dropped even with a project type: reserved names
  (`stdexec::__movable_value`, `__has_schedule_member`: the library's implementation details)
  and lambda closure types (every predicate handed to `std::ranges::find_if` is an
  `std::indirect_unary_predicate`). On the Sourcetrail slice below these two rules took 144 edges
  to 108.
- **Location.** One per specialization: its point of instantiation when that is in a project
  file, else the use that named it; for a concept-id, the concept's name in it. Activating the
  edge shows where the proof is.
- **Dedup and refresh.** One edge per (type, concept), one occurrence per proving location. The
  edges are indexed facts like any other, so they are cleared with the files that hold their
  occurrences: the file that proves `Circle -> Shape` (render.cc) is usually neither the type's
  (circle.h) nor the concept's (shape.h). Clearing render.cc removes the edge and reindexing it
  records it again (or not, if the proof is gone); a change to circle.h reindexes circle.h and
  the files that include it, which recreate the edge; merging shards is the same inject, so the
  edge is one with the occurrences of both. The storage tests cover these cases.

### Measured on a slice of Sourcetrail itself

38 translation units from this repository's compilation database (`lib_core`'s state
machines, indexer, execution, the source groups, the state machine and execution test suites,
three Qt views), project `~/dev/github/natyamatsya/sourcetrail-concepts-test`: 2,403 files,
83k edges, indexed in 10 s. **108 satisfies edges, 18 concepts, 93 types, 161 occurrences.**

| concept | types |
|---|---|
| `EnumType` (utilityEnum.h) | 24: `NodeKind`, `Edge::EdgeType`, `ReferenceKind`, `DockSide`, ... and Qt's `Qt::ConnectionType`, `QSizePolicy::Policy` |
| `MachineSpec` / `MachineSpecWithContext` | 13 / 11: `AppMachine`, `DocksMachine`, `SearchMachine`, `IndexingMachine`, ..., the test suite's `LampMachine` |
| `RemoteMachine` / `DrivenRemoteMachine` | `AppStateMachine`, `ActivationStateMachine`, `IndexingStateMachine`, the tests' `Lamp` |
| `PresentsQuestions` | `QtDialogView`, `Dialogs`, `WindowWithDialogs`, `FakeQuestionPresenter` |
| `PresentsSearch` / `PresentsDocks` / `PresentsIndexing` | `QtSearchView` / `QtDockControl` / `QtDialogView`, each with its test fake |
| `IndexerCommandCodecC` | the Rust, Swift and Zig indexer command codecs |
| `glz::read_supported` / `write_supported` (glaze) | the state machines' JSON argument and context structs (18 / 10) |
| `stdexec::scheduler` / `sender` / `sender_in` | `execution::Strand::Scheduler`, `SerialQueue::Scheduler`, `SharedQueuePool::Scheduler`, `QuestionSender`, ... |

**Not found, and why:** `IndexerCommandC` (the `IndexerCommand` constructor template's
constraint) has no edge, because every `IndexerCommand` is built by `std::make_shared<
IndexerCommand>(...)`: the constrained constructor is called inside the library's code, which is
not traversed. The same holds for anything constructed or called through `emplace_back`,
`std::invoke`, `std::visit`: the proof is in an instantiation of a non-indexed template. What
the project's own code calls or names is seen.

### Not done

- Concept refinement (`concept Solid = Shape<T> && Massive<T>` makes Solid imply Shape), which
  would be a concept -> concept edge and a hierarchy of concepts.
- Satisfaction proven only inside library instantiations (above).
- Constraints that need substituting (`Shape<T*>`), disjunctions, constraints of a member
  template on its enclosing class template's parameters.
- Explicit instantiations and explicit specializations get no handling of their own; whether
  the type their declaration names is visited as a use was not checked.
- A trail is drawn with curves, which do not draw dashes (the same for every dashed kind).

## Part 2 — static dispatch through template instantiations (investigated)

**Question:** does the C++ indexer record what an instantiated template calls, e.g. `render<
Circle>`'s `t.draw()` -> `Circle::draw`, and from where to where?

**Yes — from the implicit specialization, not from the template.** With

```cpp
template <Shape T> void render(T& t) { t.draw(); helper(t); }
template <typename T> void helper(T& t) { t.draw(); }
template <typename T> struct Painter { T shape; void paint() { shape.draw(); } };
void use() { Circle c; Square s; render(c); render(s); Painter<Circle> p; p.paint(); }
```

the index holds (a probe project under `sourcetrail-concepts-test/static-dispatch`):

- **Nodes:** each instantiation is its own node, `render<Circle>`, `render<Square>`,
  `helper<Circle>`, `Painter<Circle>`, `Painter<Circle>::paint`, with definition kind IMPLICIT,
  and a `TEMPLATE_SPECIALIZATION` edge to its primary (`render<Circle> -> render<Shape T>`,
  `Painter<Circle>::paint -> Painter<typename T>::paint`).
- **Edges off the instantiation:** `CALL render<Circle> -> Circle::draw` and `-> helper<
  Circle>`, `CALL helper<Circle> -> Circle::draw`, `CALL Painter<Circle>::paint ->
  Circle::draw`. Their occurrences are at the **primary's** call site (`4:42`, the `draw` in
  the template body), shared by every instantiation: Sourcetrail traverses instantiations of
  project templates and records them with the template's source locations.
- **The primary itself:** a dependent call (`t.draw()`, `shape.draw()`) records no CALL — the
  member is unresolved there; `Painter<T>::paint` only has a USAGE of the field `shape`. So the
  primary has no edge to any `draw`, which is why the graph of `render` shows none today.
- **Callers:** `use` calls `render<Circle>` and `render<Square>`, not `render`.

On bgfx's index (`sourcetrail-dispatch-test`): 78,241 implicit function/method specializations
of 2,585 primaries make 93,971 calls their primary does not make (64,562 distinct (primary,
target) pairs). Most go to other implicit specializations (a container's internals calling
each other); **5,795 calls / 2,129 pairs go to targets that are not themselves
specializations**, 244 of those from bgfx's and bx's own templates, e.g.
`ChunkedScratchBufferT<Derived, ...>::... -> mtl::ChunkedScratchBufferMtl::flushChunk` (CRTP),
`TimerResultT<ResultT>::reset -> mtl::TimerQueryMtl::Result::reset`,
`faultInFromCache<...> -> mtl::VideoDecoderMtl::decode`.

**Conclusion: static dispatch edges are a pure SQL pass over existing edges, like the dispatch
pass; the indexer needs nothing new.** Sketch, as a second derived kind (`EDGE_STATIC_DISPATCH`,
primary -> target, horizontal like a call):

```sql
-- implicit specializations of functions and methods, with their primary
CREATE TABLE spec AS
  SELECT ts.source_node_id AS spec_id, ts.target_node_id AS primary_id
  FROM edge AS ts
  JOIN symbol ON symbol.id = ts.source_node_id AND symbol.definition_kind = 1  -- IMPLICIT
  JOIN node ON node.id = ts.source_node_id AND node.type IN (4096, 8192)      -- function, method
  WHERE ts.type = 128;                                                        -- TEMPLATE_SPECIALIZATION
-- what the instantiations call that the primary does not call itself (its non-dependent calls)
INSERT INTO static_call
  SELECT spec.primary_id, call.target_node_id, spec.spec_id, call.id
  FROM spec JOIN edge AS call ON call.source_node_id = spec.spec_id AND call.type = 8
  WHERE NOT EXISTS (SELECT * FROM edge AS own WHERE own.type = 8
                    AND own.source_node_id = spec.primary_id
                    AND own.target_node_id = call.target_node_id);
-- then, as in deriveDispatchEdges: one edge per (primary, target), new element ids, and the
-- call edges' occurrences copied (they already sit at the primary's call site)
```

What it would need beyond that, all storage/UI-side:

- **The label (the type argument).** The edge joins `render` to `Circle::draw`; *which*
  instantiation is the specialization node (`render<Circle>`), so either keep its id with the
  edge (an `element_component` holding it, as the dispatch facts do) or let the tooltip/code view
  name it; the probe index had no `TYPE_ARGUMENT` edges to read the argument from.
- **Noise control.** Restrict to targets that are not implicit specializations (or whose node is
  in a project file), else every container's internals appear; and decide whether a call chain
  through several layers of templates (`render<Circle> -> helper<Circle> -> Circle::draw`) should
  be collapsed to `render -> Circle::draw` (a recursive step over `spec`, not done in the
  sketch).
- **Refresh.** Recompute from scratch like the dispatch pass, in the same place.
- **Cost on bgfx:** the second statement took 5.4 s in `sqlite3` (on a copy of the index, with
  its read-mode indices), almost all of it the `NOT EXISTS` over all 93,971 calls; restricting
  to non-specialization targets first should bring it near the dispatch pass's 0.3 s (not
  measured).
