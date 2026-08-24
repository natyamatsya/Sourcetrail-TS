# Mirroring the index into LadybugDB — what it actually buys

- **Status:** experiment record, run 2026-08-24. Descriptive, not a plan.
- **Question:** `ROADMAP_ANALYSIS_ENGINES.md` puts LadybugDB (Kùzu) at Tier 1 on the
  strength of deep traversals, and gates the decision on a dual-compare harness that
  had never been built. This is that comparison, at a smaller scale than the roadmap
  describes: the node/edge graph only, queried side by side rather than diffed.
- **Measurement setup:** this repo's own index, `Sourcetrail.srctrl.db` — 40,341
  nodes, 201,459 edges (76,025 `EDGE_CALL`, 38,623 `EDGE_TYPE_USAGE`, 32,483
  `EDGE_MEMBER`, 28,728 `EDGE_INCLUDE`). Apple Silicon, release builds of both
  engines. Every figure is the best of three runs of engine-internal execution time
  (`sqlite3 .timer on` real; Ladybug's own `executing` figure), which excludes process
  startup on both sides.
- **Related:** [ROADMAP_ANALYSIS_ENGINES.md](ROADMAP_ANALYSIS_ENGINES.md) (the plan
  this tests), [INDEXING_OPTIMIZATIONS.md](INDEXING_OPTIMIZATIONS.md) (the write-side
  measurements).

## Headline

**The largest win available was a missing index, not a new engine.** Sourcetrail
registered `edge(source_node_id)` / `edge(target_node_id)` under `STORAGE_MODE_CLEAR`
only, and `setMode()` drops whatever does not match the current mode — so opening a
project for reading actively deleted them, and every traversal scanned all 201k edges.
Adding `STORAGE_MODE_READ` (commit `74249d97`) takes a depth-3 transitive-callee query
from 47.0 ms to 1.3 ms, and that beats Ladybug on the same query by 3.8x.

**Ladybug's win is real but sits on a different workload than the roadmap claims.**
It loses interactive single-source traversal and wins whole-graph pattern matching.

## Results

Both engines returned identical answers on every query (distinct reachable callees:
230 / 321 / 404 / 497 at depths 1–4).

### Single-source traversal — what the navigation UI does

Transitive callees from node 172436 (230 direct callees), ms:

| depth | SQLite, no edge index | SQLite, `edge(source_node_id)` | Ladybug |
|---|---|---|---|
| 1..1 | 9.8 | **0.7** | 4.1 |
| 1..3 | 47.0 | **1.3** | 4.9 |
| 1..5 | 47.3 | **2.1** | 5.6 |
| 1..8 | 48.5 | **3.8** | 7.3 |

Reverse direction (who transitively calls node 309468, called 1,491 times), depth 1..6:
**1.9 ms** indexed SQLite against **7.1 ms** Ladybug.

A composite `(type, source_node_id, target_node_id)` index was measured too — 0.6 /
1.0 / 1.6 / 2.9 ms — marginally faster than the single-column index for ~2 MB more.
Not worth a new index definition; the shipped one captures the win.

### Whole-graph pattern matching — analytics

| query | SQLite + index | Ladybug | rows |
|---|---|---|---|
| 2-hop call pairs | 54.1 | **29.6** | 82,279 |
| 3-hop call pairs | 104.6 | **44.8** | 113,157 |
| triangle (mutual recursion) | 68.2 | **15.0** | 92 |

The advantage widens with hop count — 1.8x, 2.3x, 4.5x. This is the factorized /
worst-case-optimal join claim, and it holds.

### Sync cost

| | |
|---|---|
| CSV export + bulk `COPY` of the whole graph | **0.5 s** |
| Resulting database | 17 MB (against 59 MB SQLite, which also holds locations and occurrences) |

## Findings

1. **The read path had no edge index.** Fixed. `HierarchyCache` compensated in memory
   for the member hierarchy — and that in-memory cache is precisely what the roadmap
   proposes replacing with Cypher. An index gets there without a second engine.

2. **The two engines win different workloads.** Point-rooted traversal — one symbol,
   walk outwards, which is every click in the UI — goes to indexed SQLite by 2.5–7x.
   Whole-graph pattern matching goes to Ladybug by 1.8–4.5x. The roadmap leads with
   the first and justifies Tier 1 with it; the measurement says the second is the real
   case.

3. **Query compilation is a fixed per-query tax.** Ladybug reports 10–19 ms
   *compiling* against 5–45 ms executing. For interactive navigation the compile alone
   exceeds an entire indexed-SQLite query. Prepared statements amortise it, but only if
   they are cached.

4. **The mirror as scaffolded cannot load this graph.**
   `LadybugConnection::execute(cypher, params)` calls `prepare()` on every invocation
   and discards the statement, and `PersistentStorage::addNodes`/`addEdges` call it per
   row — ~241k compilations for this project. Measured: bulk `COPY` of 5,000 nodes,
   **160 ms**; the same 5,000 as individual `CREATE` statements *inside a single
   transaction*, **timed out at 400 s and rolled back with 0 rows**. Transaction
   batching is not the fix; statement caching or `COPY` is.

5. **One `Edge` table with a `type` property costs about 5x.** The scaffold's schema
   against per-type relationship tables, 2-hop whole-graph: **154.4 ms** against
   **29.3 ms**. Variable-length single-source was comparable (5.9 ms against 5.3 ms)
   once written with Ladybug's rel-predicate syntax. If the mirror is kept, it should
   emit a relationship table per edge type.

6. **Single writer.** The database could not be queried while a load held the write
   lock — consistent with the scaffold's framing of the mirror as a read accelerator
   rather than a concurrent store.

## What this means for Tier 1

The roadmap's stated gate — "do this once *traversal* perf/features are the
bottleneck" — is not met, and is now further away than it looked, because the
traversal bottleneck was an index rather than the engine. The surviving arguments for
Ladybug are whole-graph analytical querying (measured above) and the vector index
(examined separately, see the roadmap's Tier 1 section). Both point at the derivation
work the roadmap already sequences *first*, not at replacing the storage backend.

Nothing here argues against Ladybug. It argues that the order in the roadmap is right
and the justification attached to it is not.

## Limitations

- Node/edge only. Source locations, occurrences, local symbols and component accesses
  were not mirrored, so this says nothing about the full storage surface.
- One project, one machine, warm cache, single-threaded client. The Ladybug database
  is 17 MB and fits in memory; a project large enough to be I/O-bound could reorder
  things.
- Queries were hand-written analogues of what Sourcetrail does, not captured from the
  application. The custom-trail and hierarchy paths were not exercised as the app
  issues them.
- No dual-compare harness in the roadmap's sense — results were checked by counting,
  not by diffing graphs.

## Reproducing

```sh
DB=Sourcetrail.srctrl.db
sqlite3 -csv -noheader $DB "select id, type, coalesce(serialized_name,'') from node" > node.csv
# Kùzu wants FROM,TO first on relationship tables
sqlite3 -csv -noheader $DB "select source_node_id, target_node_id, id from edge where type=8" > call.csv

lbug graph.lbug <<'CYPHER'
CREATE NODE TABLE Node(id INT64, type INT64, name STRING, PRIMARY KEY(id));
CREATE REL TABLE Calls(FROM Node TO Node, id INT64);
COPY Node FROM 'node.csv';
COPY Calls FROM 'call.csv';
MATCH (a:Node)-[:Calls*1..3]->(b:Node) WHERE a.id=172436 RETURN count(DISTINCT b.id);
CYPHER
```

The SQLite side of the same question:

```sql
WITH RECURSIVE r(n,d) AS (
    SELECT target_node_id, 1 FROM edge WHERE type=8 AND source_node_id=172436
  UNION
    SELECT e.target_node_id, r.d+1 FROM edge e JOIN r ON e.source_node_id=r.n
      WHERE e.type=8 AND r.d < 3)
SELECT count(*) FROM (SELECT DISTINCT n FROM r);
```
