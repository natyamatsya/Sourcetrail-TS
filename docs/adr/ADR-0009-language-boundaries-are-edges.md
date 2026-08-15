# ADR-0009: A language boundary is an edge to a contract atom, never an identity merge

- **Status:** Accepted
- **Date:** 2026-08-15
- **Deciders:** natyamatsya
- **Related:** [ADR-0003](ADR-0003-ipc-flatbuffers-robustness.md) (the IPC contract every
  producer shares), `context/DESIGN_XLANG_BOUNDARIES.md` (the design this rule
  comes from), `src/lib_core/data/name/NameDelimiterType.h`,
  `src/lib_core/data/graph/Edge.h`, `src/lib_core/data/parser/LanguageMask.h`

## Context

Node identity in this storage is the serialized name and nothing else, at all
three merge layers (`IntermediateStorage::addNode`, `Storage::inject`,
`SqliteIndexStorage::addNodes`). Two indexers that emit the same name produce one
node — silently, with the node type widened to the larger of the two and the
modifier bitmasks OR-ed together.

That behaviour is correct for exactly one thing today: file nodes, which every
indexer names identically on purpose. For everything else it is a hazard, and a
measured one — a C++ `struct Widget` and a Rust `struct Widget` at equal
qualified names merge into a single node (`context/DESIGN_XLANG_BOUNDARIES.md`,
X1). Functions and fields escape only because C++ serializes a signature into the
name and the other producers do not.

With four language indexers writing into one graph, "these two declarations are
the same runtime entity" is a relation we now want to record deliberately. The
temptation is to record it the way the storage already accidentally records it:
let both sides emit one name and let the merge join them. That would destroy the
thing being modelled — after a merge there is one node and no boundary to look
at, inspect, or filter on.

## Decision

1. **A boundary is an edge, never an identity merge.** When two declarations in
   different languages denote one runtime entity, each keeps its own node, its
   own name and its own signature, and they are related by `EDGE_BINDS`. No
   producer may arrange for two declarations to share a serialized name in order
   to express a relationship.

2. **The relation is mediated by a contract atom.** The shared entity — a C ABI
   symbol — is its own node, minted in a reserved name namespace
   (`NameDelimiterType::ABI`). Reserved means no ordinary declaration may ever be
   spelled that way, which is what makes the atom namespace the *one* place where
   a deliberate name merge is the intent: two producers emitting `abi:tsq_open`
   are meant to meet. n participants cost n edges rather than n².

3. **`EDGE_BINDS` points declaration → atom**, from every producer, always. An
   atom's incoming edges answer "who implements or declares this"; its outgoing
   side stays empty. This follows the precedent set for `EDGE_MACRO_USAGE`, where
   cross-indexer agreement on direction beat each producer's local preference.

4. **Only declared linkage mints an atom.** The source must say it is reachable
   under a linkage name: C++ `extern "C"` (`FunctionDecl::isExternC`), Rust
   `#[no_mangle]` or `#[export_name]`, Zig `export`/`extern`, Swift `@_cdecl`. A
   name that merely looks foreign is not evidence, and a plain `extern "C" fn` in
   Rust is not either — without `#[no_mangle]` its symbol is still mangled and
   nothing can bind to it.

5. **Atoms carry no definition.** Nobody defines an ABI symbol; declarations bind
   against it. Atoms are recorded with `DefinitionKind::NONE`, the existing
   treatment for referenced-but-undefined symbols.

6. **File nodes remain the one deliberate identity merge.** Every indexer emits
   `/\tm<path>\ts\tp` for a file, and a file touched by two languages is one node.
   This rule does not change that and does not extend it.

## Consequences

**Positive.** The boundary is a thing you can select, and both sides survive with
their own identities, signatures and locations — a C++ declaration keeps
`(const char*)` while the Rust definition keeps its own shape. Resolution needs
no resolver: the existing name merge does the joining, inside a namespace only
atoms occupy. A one-sided boundary (an `extern fn` for a symbol nobody in the
project defines) is representable and visible rather than silently absent.

**Negative.** Every producer must agree on how an ABI symbol is spelled, or two
halves of one boundary will not meet — the failure is silent, exactly like the
collisions this rule otherwise avoids. Zig in particular must *not* apply its
file-path prefix to atom names, an exception to its own collision-avoidance rule.
The atom namespace is a new invariant to police: if any producer ever spells an
ordinary declaration with the `abi` delimiter, an atom will merge with a real
symbol and type widening will corrupt it.

**Neutral.** The rule says nothing about *inferring* boundaries — matching by
signature, following a build graph, or resolving `@cImport` contents. Those
remain derived relations and belong to the analysis-engine path in
`ROADMAP_ANALYSIS_ENGINES.md`, which emits new edge kinds into this same storage.
