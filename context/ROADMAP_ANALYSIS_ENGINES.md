# Roadmap: graph + Datalog analysis engines

An evaluation of two technologies for evolving Sourcetrail-TS beyond a code
*navigator* toward a lightweight code *analysis* platform:

- **LadybugDB** (a rebrand of **Kùzu**) — an embeddable, columnar graph database.
- **Datalog / fixpoint engines** — declarative recursive derivation, the canonical
  pairing for program analysis (Doop for points-to; CodeQL is Datalog-flavored).

They are not competitors. They sit on **two different tiers**, and keeping them
separate is the whole point:

| Tier | Technology | What it changes |
|---|---|---|
| Storage + traversal | LadybugDB / Kùzu | A better engine for the graph we *already* index (faster/native deep traversals). |
| Derivation / semantics | Datalog / fixpoint | *New* facts we can't produce today (dispatch resolution, reachability, dataflow). |

Kùzu makes existing capabilities faster; **Datalog adds capability**. That is why
Datalog is the deeper, more differentiating bet.

## Grounding: Sourcetrail is already a code graph

The model is a property graph:
`node(id, type, serialized_name)`, `edge(id, type, source_node_id, target_node_id)`
with edge types `MEMBER / CALL / INHERITANCE / OVERRIDE / TYPE_USAGE /
TYPE_ARGUMENT / TEMPLATE_SPECIALIZATION / MACRO_USAGE`, plus
`occurrence` / `source_location`.

The expensive queries are all **recursive graph queries**:
- call hierarchy (transitive callers/callees),
- inheritance closure — hand-rolled in `HierarchyCache` (an in-memory transitive
  closure of the member/parent hierarchy),
- the **custom trail** — paths between two symbols.

Today these are C++ graph code + ad-hoc SQL + in-memory caches, because SQLite
recursive CTEs are awkward and slow at graph scale. Both technologies attack
exactly this seam.

Relevant to both: the SQLite storage layer is now **pluggable** behind the
`StorageDb`/`StorageStmt`/`StorageQuery` aliases (see
[DESIGN_TURSO_BACKEND.md](DESIGN_TURSO_BACKEND.md)), and there is a **dual-write
comparison harness** that indexes into two backends and diffs the graphs. That
seam + harness are the de-risking tools for trying any alternate backend.

---

## Tier 1 — LadybugDB / Kùzu (storage + traversal)

**What it is.** An embeddable (in-process, serverless) columnar graph database,
primarily C++, Cypher query language, with factorized / worst-case-optimal join
processing, ACID transactions, full-text + vector indexing, and WASM bindings.
Formerly Kùzu.

**Where it fits Sourcetrail:**
- **Variable-length path queries** (`MATCH (a)-[:CALL*1..5]->(b)`) replace
  `HierarchyCache` and the hand-written traversals. The **custom-trail** feature
  *is* graph pathfinding — a Cypher one-liner instead of bespoke BFS + layout.
- **Factorized / WCO joins** are built for the many-to-many blow-up of graph
  pattern queries — Sourcetrail's dense reference graphs are the pathological
  SQL-join case.
- **WASM bindings** → the same engine could back a browser / claude.ai code view.
- **Vector index** → semantic symbol/doc search alongside the exact index.
- **Embeddable + C++** → it drops into the exact slot SQLite/Turso occupy, and the
  core is C++.

**Costs / risks:**
- A full storage-backend swap is a *large* lift (the Turso work is the proof).
  You would likely keep a relational store for non-graph data, so it means two
  engines or a bigger migration.
- **LadybugDB is a young fork of Kùzu** — maturity/maintenance risk vs. tracking
  Kùzu directly. Evaluate against upstream Kùzu too.
- Cypher is a second query dialect to own.

**De-risking:** reuse the storage seam + dual-compare harness — index into SQLite
and Kùzu, diff the graphs (counts + histograms) exactly as done for Turso, before
committing to a migration.

---

## Tier 2 — Datalog / fixpoint (derivation) — the deep cut

**Why the match is tight.** Sourcetrail's base tables *are* the EDB (extensional
relations); the recursive queries it fakes in C++ are the IDB (derived relations).
Program analysis + Datalog is a canonical pairing (Doop, CodeQL) precisely because
"derive new facts by recursive rules over a code relation" is what both do.

### The killer first analysis: dispatch resolution via CHA

Sourcetrail already indexes `INHERITANCE`, `OVERRIDE`, and `CALL` edges. Today a
virtual / trait-method call edge points at the *declared* method, not the real
targets. A handful of Datalog rules turn the edges we already have into resolved
implementations:

```
subtype(A, A).
subtype(A, C)      :- inheritance(A, B), subtype(B, C).
overrides(M2, M1)  :- override_edge(M2, M1).

// a virtual call to DeclM on receiver type RecvT can reach any override
// of DeclM defined on a subtype of RecvT (class-hierarchy analysis)
reaches(Call, Impl) :- virtual_call(Call, DeclM, RecvT),
                       subtype(SubT, RecvT),
                       method_of(Impl, SubT),
                       overrides(Impl, DeclM).
```

That single rule set delivers "go to the *actual* implementations through
virtual / trait dispatch" — a navigation feature no storage-engine tuning can
produce, computed from inputs Sourcetrail already has. It is the cleanest possible
demonstration of the thesis and has near-zero blast radius (emit `reaches` as a
new derived edge kind into the existing store).

### What the same engine then unlocks
- **Transitive reachability** → dead-code hints, "is X reachable from an entry
  point."
- **Reverse reachability** → **impact analysis** ("what breaks if I change this
  signature").
- **Taint / dataflow** → a security lens (source → sink), the CodeQL-style use.

Sourcetrail becomes a lightweight analysis platform, not just a navigator.

### Engine choice — the Rust indexer is the lever

Because the indexer is already Rust, Datalog can be embedded **in-process** with no
new runtime:

- **Ascent** — Rust proc-macro Datalog → compiled Rust, supports lattices /
  aggregation. Best "start here."
- **Datafrog** — minimal, fast; the engine behind **Polonius** (rustc's next-gen
  borrow checker). Good for a specific bolted-on analysis.
- **Soufflé** — Datalog → parallel C++; industrial, the Doop backend. Link from
  the C++ side for heavier analyses.
- **Differential Dataflow / DDlog** — the **incremental** substrate. This is where
  it gets deep: Sourcetrail's whole value is incremental re-index (the flag-aware
  refresh). Incremental Datalog maintains the *derived* relations under a file
  edit — recompute only what changed, not the whole closure. Correct long-term
  answer; heaviest to adopt.

**Costs / risks:** a new language + toolchain to own; the incremental story
(Differential) is genuinely hard; derived facts must be invalidated correctly on
re-index.

---

## How they compose

Not either/or: **Datalog derives** (dispatch targets, reachability), and those
derived relations get **stored** — either as new edge kinds in SQLite/Turso, or
materialized into Kùzu and served with Cypher:

```
parse → base facts (EDB) → Datalog rules → derived relations (IDB) → graph store → graph view
```

---

## Phased roadmap (de-riskable)

1. **In-indexer batch Datalog for dispatch resolution + reachability**
   (Ascent or Datafrog). Cheapest high-value cut: contained to the Rust indexer,
   reuses existing edges, emits new derived edge kinds into the current storage,
   immediately visible in the graph view. Validates "Datalog for code" with
   near-zero blast radius. **Start here.**
2. **Kùzu / LadybugDB as an alternate graph backend** behind the storage seam,
   de-risked with the dual-compare harness; move deep traversals + custom-trail to
   Cypher; unlock WASM web + vector search. Do this once *traversal* perf/features
   (not analysis) are the bottleneck.
3. **Incremental derivation (Differential Dataflow / DDlog)** to maintain derived
   relations under incremental refresh. The deep, correct end state; only when
   batch re-derivation becomes the cost.

## Open questions

- Is Kùzu upstream (vs. the LadybugDB fork) the better long-term dependency?
- Where should derived relations live and how are they invalidated on incremental
  re-index (before Differential Datalog exists)?
- Do the base edges carry enough type/receiver info for CHA, or does the indexer
  need to emit a couple of extra relations (e.g. `virtual_call(Call, DeclM,
  RecvT)`, `method_of(Impl, T)`)?
- Which language first — the Rust indexer has the cleanest path; C++/clang would
  need Soufflé or a shared derived-facts format.

## References

- Doop — declarative (Datalog) points-to analysis for Java.
- CodeQL — object-oriented query language over a relational code model, fixpoint
  evaluation.
- Polonius / Datafrog — Datalog-based borrow checking in rustc.
- Kùzu — embeddable graph DBMS with factorized query processing.
