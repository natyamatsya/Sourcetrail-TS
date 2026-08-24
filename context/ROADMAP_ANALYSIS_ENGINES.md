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
processing, ACID transactions, and full-text + vector indexing. Formerly Kùzu.

**Where it fits Sourcetrail:**
- **Variable-length path queries** (`MATCH (a)-[:CALL*1..5]->(b)`) replace
  `HierarchyCache` and the hand-written traversals. The **custom-trail** feature
  *is* graph pathfinding — a Cypher one-liner instead of bespoke BFS + layout.
- **Factorized / WCO joins** are built for the many-to-many blow-up of graph
  pattern queries — Sourcetrail's dense reference graphs are the pathological
  SQL-join case.
- **Vector index** → semantic symbol/doc search alongside the exact index.
  **Verified 2026-08-24, and it is the strongest surviving argument — see below.**
- **Embeddable + C++** → it drops into the exact slot SQLite/Turso occupy, and the
  core is C++.

### The vector index is real, maintained, and aimed at our query shape

Checked because the archival made every inherited Kùzu claim suspect.
`extension/vector` implements **HNSW** (`CREATE_VECTOR_INDEX`; metrics `cosine`,
`l2`, `l2sq`, `dotproduct`) and ships in `EXTENSION_LIST`. It is not inherited
and idle — every date below post-dates Kùzu's archival on 2025-10-10:

| Date | Work |
|---|---|
| 2026-08-14 | error-message fix |
| 2026-07-31 | direct INT8 HNSW support + correctness tests |
| 2026-07-06 | **scalar quantization storage for HNSW** (SQ8/SQ16) |
| 2026-06-04 | **NaviX adaptive search on by default** |
| 2026-06-03 | deleted-embedding handling, stranded-search fallback |

Scalar quantization is a design of LadybugDB's own: SQ8/SQ16 quantized distance
evaluation with the quantized embeddings held *in Ladybug storage* rather than a
sidecar file, transactionally consistent with the base node table, with optional
full-precision rerank.

**NaviX matters more for us.** It is filtered vector search *through the graph* —
the case where a similarity query is constrained by a graph predicate. Their own
SIFT benchmark (k=10, efs=96, 32 threads, `tools/benchmark/navix/`):

| Selectivity | NaviX recall / ms | Vanilla `auto` recall / ms |
|---:|---:|---:|
| 0.50 | 0.990 / 60.2 | 0.925 / 28.7 |
| 0.30 | 0.998 / 74.0 | 0.999 / 94.2 |
| 0.10 | 1.000 / 33.1 | 1.000 / 63.4 |

Low selectivity — few nodes surviving the filter — is roughly twice as fast at
equal recall. That is exactly the shape of a code-search query: *vectors similar
to this, but only within these files / this symbol kind / this language.* A
bolted-on vector store beside SQLite cannot do that; it would filter after the
fact, or filter first and lose the index.

**Two caveats before treating this as available.** It is an **extension, not
core**: `BUILD_EXTENSIONS` is empty by default and we build none, so using it
means building `vector` and either loading it dynamically or static-linking it —
`extension_config.cmake` only static-links it for WASM, Android and Swift, so an
embedded static build needs that path extended, and it pulls the `extension`
submodule into our build for the first time. And this was a **capability audit,
not a trial**: no HNSW index has been built over a Sourcetrail index, so
"semantic symbol search alongside the exact index" remains an untested claim
about a capability that demonstrably exists.

**Costs / risks:**
- A full storage-backend swap is a *large* lift (the Turso work is the proof).
  You would likely keep a relational store for non-graph data, so it means two
  engines or a bigger migration.
- **There is no upstream to fall back to.** Kùzu was archived by its own
  maintainers (`kuzudb/kuzu`, `archived: true`, last push 2025-10-10, final
  release 0.11.3; the README says the team "is working on something new").
  LadybugDB is the continuation, not a fork competing with a live upstream — it
  sits at 0.20.0, nine minor versions past where Kùzu stopped, carrying storage
  and query work Kùzu never shipped. So the risk is not "which of two projects"
  but "one young project, no fallback": if LadybugDB stalls, the exit is a fork
  we maintain ourselves, and the engine is a very large C++ codebase.
- Cypher is a second query dialect to own.

**De-risking:** reuse the storage seam + dual-compare harness — index into SQLite
and Kùzu, diff the graphs (counts + histograms) exactly as done for Turso, before
committing to a migration.

### Dependency provenance — vendored vs. vcpkg

Kùzu vendors ~23 libraries as trimmed source trees under `third_party/`, pinned
years behind upstream (httplib 0.14.2 vs. 0.53.1, mbedtls 3.1.0 vs. 4.2.0,
spdlog 1.13.0 vs. 1.17.0). Upstream keeps them pinned deliberately and our fork
carries no patches there, so re-vendoring by hand would be pure divergence for
no benefit — while our own vcpkg baseline already ships most of them at current
versions. Taking them from vcpkg *is* the dependency update.

The migration is per library, gated by `LBUG_SYSTEM_<LIB>` options that default
to the vendored copy, because Kùzu's own wasm, musl and python-wheel pipelines
have no package manager. Sourcetrail turns the switches on for a vcpkg build;
upstream's default path is untouched, which keeps future syncs from the fork
point cheap. The seam is `lbug_link_deps` plus the `LBUG_STATIC_ARCHIVE_LIBRARIES`
list: vendored copies are `ar`-merged into `liblbug.a`, external packages stay
ordinary transitive link dependencies. Because the sources compile as OBJECT
libraries, which never see usage requirements, an external package's headers
must also be pushed onto the global include path.

**Migrated:** brotli, yyjson, zstd, lz4, antlr4_runtime.

**Blocked, and this is the axis that matters.** The vendored copy is usually not
the upstream library: Kùzu wraps each one in a namespace so its symbols cannot
collide in a static link, and its own sources then call it by that name. Header
layout is a red herring; the namespace is what decides. snappy, fast_float,
thrift and parquet take an `lbug_` prefix (`lbug_snappy::RawUncompress`,
`lbug_fast_float::from_chars`); miniz and re2 are wrapped as `miniz::` and
`lbug::`; mbedtls is renamed the same way. utf8proc goes further still and
carries API additions of ours that upstream does not have
(`utf8proc_next_grapheme`, `utf8proc_codepoint`, a wider `utf8proc_NFC`).
Switching any of these means undoing the wrapping first — a separate decision,
since the wrapping exists to make a static liblbug.a safe to link beside other
copies of the same libraries. zstd is the exception that proves the rule: only
its internal static-API header is wrapped, and we do not use it.

**Deferred on cost.** re2 is both wrapped and would pull abseil into the
dependency tree. roaring (4.5.0) and simsimd (6.0.0) are *older* in our pinned
vcpkg baseline than the vendored copies (4.5.1, 6.2.1), so switching would be a
downgrade until the baseline moves.

**Not yet examined.** httplib and fastpfor keep their upstream namespaces and
have ports at 0.31.0 and 0.3.1 against vendored 0.14.2 and 0.1.8 — the two
remaining candidates worth a look. spdlog, taywee_args and pybind11 are used
only by `tools/` and `test/`, never by the library we link.

**No vcpkg port.** alp, pcg, glob, cppjieba, pyparse, and the generated
`antlr4_cypher` parser stay vendored, or would need an overlay port.

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
   Cypher; unlock vector search. Do this once *traversal* perf/features
   (not analysis) are the bottleneck.
3. **Incremental derivation (Differential Dataflow / DDlog)** to maintain derived
   relations under incremental refresh. The deep, correct end state; only when
   batch re-derivation becomes the cost.

## Open questions

- ~~Is Kùzu upstream (vs. the LadybugDB fork) the better long-term
  dependency?~~ **Settled 2026-08-24: Kùzu is archived, so there is no choice to
  make — LadybugDB is the only live line.** What replaces the question is
  narrower and harder: what does the exit look like if LadybugDB stalls, and is
  the storage seam enough to make that a backend swap rather than a rescue? The
  dual-compare harness is worth building for that reason alone, independent of
  whether the migration ever happens.
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
