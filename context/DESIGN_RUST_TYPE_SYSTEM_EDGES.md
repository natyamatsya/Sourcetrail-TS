# Design: Type-System Edges for the Rust Indexer

Bounds, lifetimes, generic arguments, and trait-method implementations —
mapping the type-system-encoded relationships of Rust onto Sourcetrail's
graph vocabulary.

**Status: implemented** (`ea9f27d6`, 2026-07-10) — all six design points
shipped as specified; CI green on ubuntu/macos/windows (run `29092989697`).
Implementation notes that go beyond the letter of the sections below:

- `TypeBoundList` also appears under `dyn Trait` / `impl Trait` types; those
  classify as plain type positions (`BoundOwner::Item`), not param bounds.
- HRTB binders (`T: for<'a> Fn(&'a u8)`) declare lifetimes that are not
  `GenericDef` params; the lifetime walk stops at the `for<>` binder's
  `GENERIC_PARAM_LIST` so they are skipped rather than misattributed.
- Associated-type bounds in traits (`type Item: Debug`) attach to the alias
  node (`Trait::Item —type_use→ Debug`).

## Background

Rust encodes a large amount of structural information in the type system
rather than in nominal declarations: trait bounds (`T: Display`), lifetime
parameters and outlives relations (`'a: 'b`, `T: 'a`), generic arguments at
use sites (`Vec<Foo>`), and the implicit correspondence between an impl
method and the trait method it implements. As of the semantic-resolution and
reference-occurrence work (commits `82814eb5`, `6ee1d8f7`), the indexer
resolves references exactly via rust-analyzer's `Semantics` layer, but the
bound/lifetime machinery still predates it:

- Trait bounds emit `EDGE_TYPE_USAGE` from the **owning item** to the bound
  trait (`print —type_use→ Display`), discarding *which parameter* carries
  the bound.
- Bound targets resolve by **name suffix matching**, so same-named traits in
  different modules are dropped as ambiguous.
- Lifetime parameters become `NODE_TYPE_PARAMETER` nodes, but outlives
  relations between them are not recorded.
- Generic arguments at use sites and trait-method implementations are not
  modeled at all, although Sourcetrail has dedicated edge kinds for both.

Sourcetrail's vocabulary (`src/lib/data/graph/Edge.h:17-32`,
`src/lib/data/NodeKind.h`) already covers everything needed:
`NODE_TYPE_PARAMETER`, `EDGE_MEMBER`, `EDGE_TYPE_USAGE`, `EDGE_TYPE_ARGUMENT`
(1<<6, used by the C++ indexer for template arguments), `EDGE_OVERRIDE`
(1<<5, used for virtual/interface method implementations), and
`EDGE_INHERITANCE`. No schema or GUI change is required — this is purely
about pointing the right edges at the right nodes.

## Design

### 1. Generic parameters are emitted from HIR, keyed by identity

Every generic item (fn, struct, enum, union, trait, type alias, impl) emits
its parameters via `hir::GenericDef::{type_or_const_params, lifetime_params}`
instead of the AST walk:

- `NODE_TYPE_PARAMETER` named `Owner::T`, `Owner::'a`, `Owner::N`
- `EDGE_MEMBER` from the owner
- registered under `DefKey::{TypeParam, ConstParam, LifetimeParam}` so the
  semantic pass can resolve references to them exactly

The implicit `Self` parameter of traits is skipped (recognizable by its
`Either::Right(ast::Trait)` source). Impl-block parameters attach to the
impl's self-type-qualified owner name (`module::Wrapper::T`); if the type
definition declares the same parameter name, the storage layer's
dedup-by-serialized-name merges them on inject, which is the desired
rendering.

### 2. Bounds are edges from the *parameter* node

`fn print<T: Display>` reads:

    print —member→ print::T —type_use→ Display

not `print —type_use→ Display`. Clicking `Display` then answers "which
generic parameters require me". Bound emission moves into the semantic
reference pass: a `PATH_TYPE` inside an `ast::TypeBound` determines its
source node from the bound owner —

| bound owner (AST ancestor)  | source node                                    |
|-----------------------------|------------------------------------------------|
| `ast::TypeParam`            | that parameter (`to_def` → `DefKey::TypeParam`) |
| `ast::WherePred`            | the predicate's subject if it resolves to a parameter, else the enclosing item |
| `ast::Trait` (supertraits)  | the trait — emitted as `EDGE_INHERITANCE` (see 4) |

with a `TOKEN` occurrence on the edge at the bound's name token, like every
other reference.

### 3. Lifetimes: outlives relations between parameter nodes

Lifetime params are already `NODE_TYPE_PARAMETER`s; the outlives lattice
becomes visible through ordinary edges:

- `'a: 'b`  →  `Owner::'a —type_use→ Owner::'b`
- `T: 'a`   →  `Owner::T —type_use→ Owner::'a`

Lifetime *references* are resolved without a dedicated `Semantics` API: the
enclosing generic item is mapped `to_def`, its `GenericDef::lifetime_params`
are matched by name text, and the hit is looked up via
`DefKey::LifetimeParam`. Only bound positions emit edges; plain uses in
types (`&'a T`) are deliberately not recorded in v1 to control noise.

### 4. What deliberately stays INHERITANCE — and what never becomes it

`EDGE_INHERITANCE` is reserved for real subtype-ish facts: `impl Trait for
Type` and supertraits (`trait A: B`). Supertrait emission moves from the AST
walk into the semantic pass (exact resolution + occurrence), but keeps its
edge kind. Trait *bounds* must NOT use INHERITANCE, despite reading as
"conforms to": Sourcetrail's trait view lists implementors via inheritance
edges, and bounds would pollute every trait's implementor list with phantom
type parameters.

### 5. Generic arguments at use sites → EDGE_TYPE_ARGUMENT

A `PATH_TYPE` located inside a `GENERIC_ARG_LIST` (e.g. `Foo` in `Vec<Foo>`)
emits `EDGE_TYPE_ARGUMENT` instead of `EDGE_TYPE_USAGE`, from the same
source-resolution rules as above. This is the v1 of the C++ indexer's
template-argument model. The full fidelity version — implicit specialization
nodes (`Vec<Foo>` bubble with `EDGE_TEMPLATE_SPECIALIZATION` to `Vec`) —
requires emitting implicit/non-indexed nodes and is out of scope here.

### 6. Trait-method implementations → EDGE_OVERRIDE

For each `impl Trait for Type`, every impl function is matched by name
against the trait's items (name equality is guaranteed by the language) and
emits:

    Type::method —override→ Trait::method

This is exactly what `EDGE_OVERRIDE` exists for in the C++/Java indexers and
makes trait dispatch navigable: click a trait method, see every
implementation. Emitted in the semantic pass (all defs are registered by
then, regardless of declaration order). Functions only in v1; associated
consts/types stay MEMBER-only.

## What is removed

The AST bound machinery becomes fully superseded by the semantic pass and is
deleted (git history preserves it): `BoundTarget`, `bound_target()`,
`extract_lifetime_name()`, `ensure_type_parameter_node()`,
`emit_ast_generic_bounds()`, `collect_generic_bounds()`, and the AST
supertrait walk in `collect_trait_details()`.

## Verification (results)

- 73 unit tests pass, 7 of them added for this design: bound edges originate
  at `Owner::T`, not `Owner` (inline and where-clause); `'b: 'a` and `T: 'a`
  produce edges between parameter nodes; `Holder<Foo>` produces
  `EDGE_TYPE_ARGUMENT f → Foo` while `Holder` stays TYPE_USAGE;
  `Circle::draw —override→ Draw::draw`; supertraits with qualified paths
  (`trait Special: two::Marker`) resolve exactly; impl-block param bounds
  (`impl<T: Clean> W<T>`) attach to the param node.
- `index_self`: 1012 nodes, 3832 locations/occurrences, 0 errors, 0 compiler
  warnings.
- Existing disambiguation tests (same-named traits/functions across modules)
  kept passing — the string fallback only fires when semantic resolution
  fails.

## Out of scope / follow-ups

- Implicit specialization nodes (`Vec<Foo>` with TEMPLATE_SPECIALIZATION) —
  needs non-indexed node emission.
- Lifetime usage occurrences in types (`&'a T`) — revisit once noise
  tolerance is known.
- Bounds on associated-type projections (`where T::Item: Debug`) attach to
  `T` in v1, not to a projection node.
- `EDGE_OVERRIDE` for associated consts/types if the GUI proves useful for
  functions.
