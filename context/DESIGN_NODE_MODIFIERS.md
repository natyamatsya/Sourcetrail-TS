# Design: Declaration Modifiers — A Three-Axis Model

**Status: Axis 1 (visibility / `AccessKind`) and Axis 2 (capabilities /
`NodeModifier`) are implemented. Axis 3a — the per-node metadata table
(`node_attribute`, DESIGN_STORAGE_CODEGEN.md step 2) with producers/consumers for
Swift `@available` (→ `AVAILABILITY`) and **deprecation** (the cross-axis fact:
`NODE_MODIFIER_DEPRECATED` bit + `DEPRECATED` message row) — is now implemented
too. Axis 3b (config-atom nodes + guard edges), the `open`/`final` Axis-2 bits,
and the C++/Rust deprecation/availability producers remain proposed.**
This note names the general model behind three changes that shipped
piecemeal — `AccessKind::PACKAGE`, the `NodeModifier` bitmask, and
`async`/`nonisolated` — and shows that the two remaining Swift "open points"
(`open` vs `public`; `@available`) are not two problems but two axes of the same
one. It is written from the perspective of C++, Rust, Swift, and Zig so the
storage stays language-neutral as more indexers land.

## The modeling insight

Sourcetrail's storage originally had a single place for "how is this declaration
qualified": the per-node **`AccessKind`** (`src/lib/data/parser/AccessKind.h`),
a one-dimensional visibility enum. Every qualifier that is *not* plain
visibility was therefore either dropped or awkwardly forced onto that enum.

But across languages, declaration qualifiers span **three orthogonal axes**:

1. **Visibility** — where a name may be referenced.
2. **Capabilities** — orthogonal boolean qualifiers on behaviour/lifecycle.
3. **Configuration guards** — a declaration's existence/usability conditioned on
   an *external* environment (platform, version, feature, build mode).

We have already, without naming it, split the first two apart:
`AccessKind` is Axis 1, and the `NodeModifier` bitmask
(`src/lib/data/parser/NodeModifier.h`, added for Swift `actor`/`async`/
`nonisolated`) is Axis 2. The remaining open points each belong to an axis:
`open`/`final` is Axis 2, and `@available` is Axis 3 — the one primitive we have
not built yet.

## The four-language modifier zoo

| Concern | C++ | Rust | Swift | Zig |
|---|---|---|---|---|
| **Visibility** | public / protected / private; module `export` | `pub`, `pub(crate)`, `pub(super)`, `pub(in path)`, private | open / public / **package** / internal / fileprivate / private | `pub` vs file-local (binary) |
| **Subclassability** | default-open; `final` closes | (no inheritance; *sealed-trait* pattern) | **`open`** = externally subclassable; `public` = in-module only; `final` = sealed | (no inheritance) |
| **Behavioural flags** | `virtual`/`override`, `constexpr`, `noexcept`, `static`, `mutable`, `explicit` | `unsafe`, `const`, `async`, `#[non_exhaustive]` | `mutating`, `async`, `nonisolated`, `dynamic`, `required` | `comptime`, `export`, `extern`, `inline`, `threadlocal`, `callconv` |
| **Lifecycle** | `[[deprecated("msg")]]` | `#[deprecated(since, note)]`, `#[stable/unstable]` | `@available(*, deprecated/obsoleted, message:)` | (convention / `@compileError`) |
| **Config gating** | `#ifdef`, `#if`, Clang `__attribute__((availability(...)))`, `__has_include` | `#[cfg(target_os/feature/…)]`, `cfg_attr` | `@available(iOS 15, macOS 12, *)`, `#available` | `comptime` on `@import("builtin").target.{os,cpu,abi,mode}` |

Read the columns and the axes fall out on their own. Two facts about the table
matter for the storage design:

- **`@available` is not Swift-only.** Clang's `availability` attribute is the
  C/Objective-C form of the same thing (Swift's `@available` lowers onto it),
  Rust's `#[cfg]` is the same fact as an arbitrary predicate, and Zig expresses
  it as `comptime` branches on `@import("builtin").target`. Axis 3 is universal.
- **The target of a config guard is never a declaration.** `iOS`, `x86_64`,
  `feature="serde"`, `target_os="linux"` are *config atoms* — external
  environment values, not nodes in the code graph. That is exactly why
  `@available` "has no representable target": we were looking for a node and
  there isn't one, the same way an external macro or an stdlib type is "outside
  the sources."

## Axis 1 — Visibility (`AccessKind`) — implemented

A single enum, `src/lib/data/parser/AccessKind.h`: `NONE`, `PUBLIC`,
`PROTECTED`, `PRIVATE`, `DEFAULT`, `TEMPLATE_PARAMETER`, `TYPE_PARAMETER`,
`PACKAGE`. Stored per node in the `component_access` table; emitted by the C++
and Swift indexers.

*Known incompleteness:* Rust's visibility is genuinely richer — `pub(in path)`
is **scoped**, not a point on a lattice — so a full model of Axis 1 would pair
the enum with an optional scope reference. That is out of scope here, but the
enum is not the final word on visibility.

## Axis 2 — Capabilities (`NodeModifier`) — implemented, extend for `open`/`final`

A per-node bitmask, `src/lib/data/parser/NodeModifier.h`, stored in
`StorageNode.modifiers` (`intermediate_storage.fbs` → C++ `StorageNodeData` →
SQLite `node.modifiers`, round-tripped across all three serializers). Current
bits: `NODE_MODIFIER_ACTOR`, `NODE_MODIFIER_ASYNC`, `NODE_MODIFIER_NONISOLATED`.
The graph `Node` exposes them and `Node::getReadableTypeString()` composes them
("actor", "async method", "nonisolated async method").

**`open` vs `public` lives here.** The earlier framing — "`open` doesn't fit the
`AccessKind` axis" — was right about the *wrong* axis. `open` is `public`
**visibility** (Axis 1) *plus* a **capability** (Axis 2): "externally
subclassable". So:

- `open` → `AccessKind::PUBLIC` + `NODE_MODIFIER_OPEN`
- `final` → `NODE_MODIFIER_FINAL` (also C++ `final`, since C++ classes are
  default-open — the inverse default of Swift)

This needs **no new storage** — two more bits on the field that already ships.
The bit is Swift/C++-populated; Rust and Zig have no inheritance, so it simply
stays unset. The axis is universal; population is per-indexer.

This axis is the natural home for the rest of the behavioural zoo as indexers
want them: `static`, `mutating`, `override`, `required`, `dynamic`, `unsafe`,
`const`, `comptime`, `export`, `extern`, `inline`, … — each a bit, no schema
churn.

## Axis 3 — Configuration guards — proposed (the genuinely missing primitive)

The general form, common to all four languages:

> A declaration is guarded by a **predicate over a configuration space** whose
> dimensions are `{os, arch, abi, version, feature, build-mode, language-version}`.

`@available(iOS 15, *)`, `#[cfg(all(unix, feature="x"))]`, `#ifdef _WIN32`, and
`if (builtin.os.tag == .windows)` are the *same fact* in four syntaxes. Two
complementary representations, layerable:

### (a) A per-node metadata table — recommended first

`node_metadata(node_id, key, value)` — the raw and/or structured guard as text:

- `available = "iOS 15.0, macOS 12.0"`
- `cfg = "all(unix, feature=serde)"`
- `deprecated = "since iOS 16: use X instead"`

Cheap, faithful for display/tooltip, filterable. This is the **highest-leverage**
thing to build, because it is a general escape hatch: a key→value store absorbs
availability, deprecation messages, doc-comment briefs, symbol sub-kinds,
generic-constraint spellings — anything structured that does not warrant its own
column. **One new table, no per-feature schema churn**, and it degrades to
"empty" for indexers that do not emit it. It mirrors the storage shape of
`component_access` (a small side table keyed by `node_id`).

### (b) Config-atom nodes + guard edges — optional, for navigability

Make `os:iOS`, `feature:serde`, `arch:x86_64` into **synthetic definition-less
nodes** — exactly the pattern already used for external macros (`NODE_MACRO`,
`DEFINITION_NONE`, "outside the sources") — and link each guarded declaration to
the atoms it references via a guard edge (`EDGE_ANNOTATION_USAGE` reused, or a
dedicated kind), with the version/comparison as edge annotation or encoded in
the atom node's name ("iOS ≥ 15.0").

This buys *navigability* — "show me everything iOS-gated", "what breaks if I drop
feature X". The cost: compound boolean structure (`all`/`any`, per-platform `*`)
**flattens to a set** of referenced atoms; the exact predicate stays in the (a)
metadata string. For a navigation tool that 80/20 is the right call — you almost
always want "which atoms gate this", not the full boolean tree.

## Deprecation — a cross-axis fact

Deprecation is worth calling out because it touches both existing axes and Axis
3 at once, and is fully cross-language (`[[deprecated]]`, `#[deprecated]`,
`@available(*, deprecated)`; Zig by convention):

- the boolean → a capability bit, `NODE_MODIFIER_DEPRECATED` (Axis 2);
- the message / since-version → a `deprecated` row in the metadata table (Axis 3a).

**Implemented (Swift, 2026-07-18).** `NODE_MODIFIER_DEPRECATED = 1 << 3` composes
into `nodeModifierToString` ("deprecated" in the graph node label). The Swift
indexer detects `@available(*, deprecated[, message:])` / `obsoleted` /
`unavailable` (`swiftDeprecation`, both engines): it sets the bit and, when the
attribute carries a message, records a `DEPRECATED` `node_attribute`. The node
tooltip shows `[deprecated: <message>]` off the bit + the row. C++ `[[deprecated]]`
and Rust `#[deprecated]` are the remaining per-indexer producers over the same
primitive.

## Graceful degradation

The axes are universal; **emission is per-indexer**, so every indexer fills only
what its language has and unpopulated bits/rows stay empty:

- **C++** — Axis 1 (public/protected/private), Axis 2 (`final`, `virtual`,
  `static`, …), Axis 3 (`#ifdef`, Clang `availability`, `[[deprecated]]`).
- **Rust** — Axis 1 (with the scoped-visibility caveat), Axis 2 (`unsafe`,
  `const`, `async`; no `open`), Axis 3 (`#[cfg]` as arbitrary predicates that
  flatten to atoms; `#[deprecated]`).
- **Swift** — all three, densely: the whole reason this note exists.
- **Zig** — Axis 1 (binary `pub`), Axis 2 (`comptime`, `export`, `extern`, …; no
  `open`), Axis 3 (`comptime` target branches → the same atom set).

## Staging recommendation

1. **`open`/`final` now** — add `NODE_MODIFIER_OPEN` + `NODE_MODIFIER_FINAL` and
   emit them from the Swift (and later C++) indexers. Same shape as `async`;
   closes that open point immediately, zero new storage.
2. **`node_metadata` table next** — the general Axis-3 primitive. **Done** as
   `node_attribute` (DESIGN_STORAGE_CODEGEN.md step 2), with the first producer +
   consumer landed: Swift extracts `@available(...)` (both engines, purely
   syntactic so it survives broken builds) into the `AVAILABILITY` key, and the
   node tooltip appends `@available(...)`. Remaining emitters — Rust `#[cfg]` /
   `#[deprecated]`, C++ `#ifdef` / Clang availability / `[[deprecated]]` — and the
   graph *filter* are additive follow-ups over the same table.
3. **Config-atom nodes + guard edges later** — only if/when a navigable
   "configuration surface" is wanted. Reuses the synthetic-node machinery, so it
   is additive over step 2, not a rewrite.

Each step is monotonic over what exists: Axis 1 = `AccessKind` (done), Axis 2 =
`NodeModifier` (done, +2 bits), Axis 3 = one key→value table (+ optional
synthetic-node reuse). No existing concept is disturbed.

## Critical files

- `src/lib/data/parser/AccessKind.h/.cpp` — Axis 1 enum.
- `src/lib/data/parser/NodeModifier.h/.cpp` — Axis 2 bitmask (extend for
  `open`/`final`/`deprecated`).
- `src/lib/data/storage/type/StorageNode.h`, `intermediate_storage.fbs`,
  `sqlite/IndexTables.h`, `sqlite/SqliteIndexStorage.cpp` — where a new
  `node_metadata` table (Axis 3a) would live, mirroring `component_access`.
- `src/lib/data/graph/Node.{h,cpp}` — graph-model consumer + readable-string
  composition.
- `src/swift_indexer/.../AccessSyntax.swift` — the per-position modifier/access
  extractor to extend for `open`/`final` and `@available`.
- `src/rust_indexer/.../parser/collector.rs` — where Rust `#[cfg]`/`#[deprecated]`
  emission (Axis 3) would attach.
