# Roadmap: Visualization Responsiveness for Large Projects

## Background

This document captures the findings from a full static analysis of the Sourcetrail
visualization pipeline and proposes a prioritized set of improvements to make the
graph and code views responsive on large projects (100k+ nodes, 500k+ edges).

All findings are grounded in the actual source code. File references use paths
relative to the repository root.

---

## The Activation Critical Path

Every user click on a symbol triggers this sequential chain, **entirely on the UI
thread**:

```text
MessageActivateTokens
  └─ GraphController::handleMessage()                 [UI thread, blocks]
       ├─ StorageAccess::getGraphForActiveTokenIds()  ← multiple SQLite queries
       ├─ createDummyGraph()                          ← O(N nodes)
       ├─ setActive() + setVisibility()               ← O(N nodes × M edges)
       ├─ bundleNodes()                               ← O(N²) edge scan
       ├─ groupNodesByParents()                       ← N individual SQLite queries
       ├─ layoutNesting()                             ← recursive O(N)
       ├─ layoutGraph() / BucketLayouter              ← O(N × E) placement
       └─ view->rebuildGraph()                        ← Qt render

  └─ CodeController::handleMessage()                  [UI thread, blocks]
       ├─ getSourceLocationsForTokenIds()             ← SQLite queries
       ├─ getFilesForCollection()
       └─ showFiles()                                 ← file I/O + syntax highlight
```

There is no async dispatch anywhere in this chain. The UI freezes for the entire
duration of every activation.

---

## Bottleneck Catalogue

### B1 — Sequential SQLite Queries per Activation (Critical)

**File:** `src/lib/data/storage/PersistentStorage.cpp`, `getGraphForActiveTokenIds()`

A single node activation issues these SQLite round-trips in sequence:

1. `getFirstById<StorageNode>(elementId)` — node type lookup
2. `getEdgesBySourceOrTargetId(elementId)` — all edges of active node
3. `getAllByIds<StorageSymbol>(ids)` — symbol definition kinds
4. `getAllByIds<StorageNode>(ids)` — node data
5. `getAllByIds<StorageEdge>(edgeIds)` — edge data
6. `getSourceLocationsForElementIds(nodeIds)` — for parent file map
7. `getAllByIds<StorageSourceLocation>(locationIds)` — location data
8. `getEdgesBySourceIds(childNodeIds)` — bundled edge construction
9. `getEdgesByTargetIds(childNodeIds)` — bundled edge construction

`addNodesWithParentsAndEdgesToGraph()` (called on every activation) adds three
more sequential queries on top of these.

### B2 — Trail BFS: Two SQLite Queries per Depth Level (Critical)

**File:** `src/lib/data/storage/PersistentStorage.cpp`, `getGraphForTrail()`

```cpp
while (nodeIdsToProcess.size() && (!depth || currentDepth < depth))
{
    edges = getEdgesBySourceIds(nodeIdsToProcess);   // query 1
    edges += getEdgesByTargetIds(nodeIdsToProcess);  // query 2
    // ...
    nodes = getAllByIds<StorageNode>(nodeIdsToCheck); // query 3
    currentDepth++;
}
```

For depth=5 this is 15 sequential SQLite round-trips. The edge fetch and node
type filter for the same depth level are independent and could overlap.

### B3 — `bundleNodes()`: O(N²) Edge Scan (High)

**File:** `src/lib/component/controller/GraphController.cpp:1049`

```cpp
for (const auto& node: m_dummyNodes)                    // O(N)
{
    node->data->forEachNodeRecursive([&](const Node* n) {
        n->forEachEdgeOfType(~EDGE_MEMBER, [&](Edge* e) { // O(E per node)
            // classify edge direction
        });
    });
}
// then 7× bundleNodesAndEdgesMatching() calls, each O(N × E)
```

`bundleNodesAndEdgesMatching()` has a nested loop: for each matched node, scan
all `m_dummyEdges` to find connected ones. With N=500 nodes and E=2000 edges
this is ~1M iterations per activation.

### B4 — `groupNodesByParents()` NAMESPACE Path: N Individual SQLite Lookups (High)

**File:** `src/lib/component/controller/GraphController.cpp:1588`

```cpp
else if (groupType == GroupType::NAMESPACE)
{
    // called inside the loop over m_dummyNodes:
    qualifierId = m_storageAccess->getNodeIdForNameHierarchy(
        qualifierNode->qualifierName);  // ← one query per node
}
```

N separate `getNodeIdForNameHierarchy()` calls instead of one batched lookup.
The batch API `getNodeIdsForNameHierarchies()` already exists in `StorageAccess`.

### B5 — `buildCaches()`: Four Sequential Full-Table Scans (High)

**File:** `src/lib/data/storage/PersistentStorage.cpp:463`

```cpp
void PersistentStorage::buildCaches()
{
    buildFilePathMaps();         // full table scan
    buildSearchIndex();          // full table scan + trie build
    buildMemberEdgeIdOrderMap(); // full table scan
    buildHierarchyCache();       // full table scan
}
```

All four are sequential. They write to independent in-memory structures and read
from SQLite (which supports multiple concurrent readers in WAL mode). On a
500k-node project this takes several seconds and blocks the UI on project open.

### B6 — Graph and Code Controllers Run Sequentially (High)

`GraphController` and `CodeController` both handle `MessageActivateTokens` and
query completely independent data. They are dispatched sequentially on the same
scheduler. Parallelizing them would halve the perceived latency for the common
case where both views need to update.

### B7 — `layoutNestingRecursive()` Subtrees Are Sequential (Medium)

**File:** `src/lib/component/controller/GraphController.cpp:1858`

```cpp
for (const auto& node: m_dummyNodes)
    layoutNestingRecursive(node.get());  // each subtree is independent
```

Top-level `m_dummyNodes` are fully independent. Each call touches only its own
subtree. These are safe to parallelize with `std::execution::par_unseq` (C++17).

### B8 — `BucketLayouter::createBuckets()` Deque Cycling (Medium)

**File:** `src/lib/component/controller/helper/BucketLayouter.cpp:283`

When many edges connect nodes that are not yet placed, the deque-based placement
loop cycles through all remaining edges on each pass (`skipCount` / `force`
fallback), producing O(N²) behavior. A topological pre-sort of nodes by
connectivity would reduce this to O(N log N).

### B9 — `StorageCache::m_graphForAll` Cleared on Every Refresh (Medium)

**File:** `src/lib/data/storage/StorageCache.cpp:8`

The overview graph is cached but invalidated by `StorageCache::clear()`, which
is called on any refresh — including incremental re-indexing of a single file.
The cache is rebuilt from a full table scan on the next overview open.

### B10 — Tooltip: Synchronous SQLite + File I/O on Hover (Low)

**File:** `src/lib/data/storage/PersistentStorage.cpp`, `getTooltipSnippetForNode()`

Multiple SQLite queries plus a file read are issued synchronously on the UI
thread on every hover event. This causes intermittent stutter on large projects.

---

## Proposed Improvements

### Phase 1 — Async Data Fetch (Highest Leverage)

**Goal:** Keep the UI thread free during the storage query phase.

The entire `getGraphForActiveTokenIds()` + `getSourceLocationsForTokenIds()`
block should be moved to a background thread. The UI thread dispatches the
request, shows a lightweight loading indicator, and receives the completed
`Graph` + `SourceLocationCollection` objects back via a queued signal/slot or
`QMetaObject::invokeMethod`. Layout and render remain on the UI thread.

This single change makes the UI responsive during the ~100–500ms query phase
that dominates on large projects.

**Affected files:**

- `src/lib/component/controller/GraphController.cpp` — wrap storage call in `std::async` or a thread pool task
- `src/lib/component/controller/CodeController.cpp` — same
- `src/lib/app/` — add a task queue / cancellation token so stale results from
  a superseded activation are discarded

**Complexity:** High (requires careful cancellation logic)  
**Impact:** Critical

---

### Phase 2 — Parallel Graph + Code Fetch

**Goal:** Overlap the graph query and the code location query.

Since `GraphController` and `CodeController` query independent tables, their
storage calls can be launched as two concurrent futures and joined before the
UI update:

```cpp
auto graphFuture = std::async(std::launch::async, [&] {
    return storageAccess->getGraphForActiveTokenIds(tokenIds, expandedIds);
});
auto locFuture = std::async(std::launch::async, [&] {
    return storageAccess->getSourceLocationsForTokenIds(tokenIds);
});
auto graph = graphFuture.get();
auto locs  = locFuture.get();
```

Requires SQLite to be opened with WAL mode and a separate connection per thread
(SQLite connections are not thread-safe).

**Affected files:**

- `src/lib/data/storage/SqliteIndexStorage.*` — expose a connection-per-thread model
- `src/lib/component/controller/GraphController.cpp`
- `src/lib/component/controller/CodeController.cpp`

**Complexity:** Medium  
**Impact:** High

---

### Phase 3 — Batch SQLite Queries in the Hot Path

**Goal:** Reduce round-trips from ~9 sequential queries to ~3 per activation.

Several queries in `getGraphForActiveTokenIds()` fetch data that could be
combined:

1. Merge the `getAllByIds<StorageSymbol>` and `getAllByIds<StorageNode>` calls
   into a single JOIN query.
2. In `addNodesWithParentsAndEdgesToGraph()`, collect all needed node IDs first
   (from edges + hierarchy parents), then issue a single `getAllByIds<StorageNode>`
   covering all of them.
3. In the trail BFS, issue the edge query and the node-type filter query for the
   same depth level concurrently (two connections).

**Affected files:**

- `src/lib/data/storage/PersistentStorage.cpp`
- `src/lib/data/storage/sqlite/SqliteIndexStorage.*`

**Complexity:** Medium  
**Impact:** High

---

### Phase 4 — Fix `bundleNodes()` O(N²) Scan

**Goal:** Reduce bundling from O(N²) to O(N + E).

Replace the nested scan with a single O(E) pass that builds a
`nodeId → [connected DummyEdge*]` adjacency map before the bundling loop:

```cpp
// Build once, O(E)
std::unordered_map<Id, std::vector<DummyEdge*>> edgesByNode;
for (const auto& edge: m_dummyEdges)
{
    edgesByNode[edge->ownerId].push_back(edge.get());
    edgesByNode[edge->targetId].push_back(edge.get());
}

// Then each bundleNodesAndEdgesMatching() uses edgesByNode[node->tokenId]
// instead of scanning all m_dummyEdges
```

**Affected files:**

- `src/lib/component/controller/GraphController.cpp:1049–1352`

**Complexity:** Low  
**Impact:** High (pure algorithmic, no threading required)

---

### Phase 5 — Batch `groupNodesByParents()` Namespace Lookups

**Goal:** Replace N individual SQLite calls with one batched call.

```cpp
// Collect all qualifier names first
std::vector<NameHierarchy> qualifierNames;
for (const auto& dummyNode: m_dummyNodes)
{
    if (const DummyNode* q = dummyNode->getQualifierNode())
        qualifierNames.push_back(q->qualifierName);
}

// Single batched lookup
std::vector<Id> qualifierIds =
    m_storageAccess->getNodeIdsForNameHierarchies(qualifierNames);
```

`getNodeIdsForNameHierarchies()` already exists in `StorageAccess.h` but is not
used here.

**Affected files:**

- `src/lib/component/controller/GraphController.cpp:1527–1665`

**Complexity:** Low  
**Impact:** High

---

### Phase 6 — Parallel `buildCaches()`

**Goal:** Reduce project-open time by running the four cache builds concurrently.

Each of `buildFilePathMaps`, `buildSearchIndex`, `buildMemberEdgeIdOrderMap`,
and `buildHierarchyCache` writes to an independent in-memory structure. With
SQLite in WAL mode and one read connection per thread, all four can run in
parallel:

```cpp
void PersistentStorage::buildCaches()
{
    clearCaches();
    auto f1 = std::async(std::launch::async, [this] { buildFilePathMaps(); });
    auto f2 = std::async(std::launch::async, [this] { buildSearchIndex(); });
    auto f3 = std::async(std::launch::async, [this] { buildMemberEdgeIdOrderMap(); });
    auto f4 = std::async(std::launch::async, [this] { buildHierarchyCache(); });
    f1.get(); f2.get(); f3.get(); f4.get();
}
```

Requires each build function to use its own SQLite read connection.

**Affected files:**

- `src/lib/data/storage/PersistentStorage.cpp:463`
- `src/lib/data/storage/sqlite/SqliteIndexStorage.*`

**Complexity:** Medium  
**Impact:** High (project open time)

---

### Phase 7 — Parallel `layoutNestingRecursive()` Top-Level Nodes

**Goal:** Use all CPU cores for the nesting layout pass.

Top-level `m_dummyNodes` are independent — each call to
`layoutNestingRecursive(node.get())` touches only that node's subtree and writes
only to its own `DummyNode` objects. This is safe to parallelize:

```cpp
std::for_each(
    std::execution::par_unseq,
    m_dummyNodes.begin(), m_dummyNodes.end(),
    [this](const std::shared_ptr<DummyNode>& node) {
        layoutNestingRecursive(node.get());
    });
```

Requires C++17 `<execution>` and linking against Intel TBB or the platform
parallel STL.

**Affected files:**

- `src/lib/component/controller/GraphController.cpp:1852–1867`
- `CMakeLists.txt` — add TBB dependency if not already present

**Complexity:** Low  
**Impact:** Medium

---

### Phase 8 — Fix `BucketLayouter` Deque Cycling

**Goal:** Eliminate O(N²) worst-case in bucket placement.

The current algorithm places nodes into buckets by processing edges in arbitrary
order. When an edge's endpoints are both unplaced, it is pushed to the back of
the deque and retried. In the worst case every edge is retried N times.

Fix: before the deque loop, topologically sort nodes by the number of already-
placed neighbours (most-connected first). This ensures the `force` fallback is
rarely needed.

**Affected files:**

- `src/lib/component/controller/helper/BucketLayouter.cpp:247–363`

**Complexity:** Medium  
**Impact:** Medium

---

### Phase 9 — Smarter `StorageCache::m_graphForAll` Invalidation

**Goal:** Avoid full table scan on overview re-open during incremental indexing.

Currently `StorageCache::clear()` resets `m_graphForAll` unconditionally. Add a
dirty flag that is set only when the node/edge count actually changes:

```cpp
void StorageCache::invalidateGraphForAll()
{
    m_graphForAll.reset();
}
```

Call `invalidateGraphForAll()` only from `finishInjection()` / `clearFileElements()`,
not from the general `clear()` path triggered by UI refresh.

**Affected files:**

- `src/lib/data/storage/StorageCache.cpp`
- `src/lib/data/storage/StorageCache.h`
- `src/lib/data/storage/PersistentStorage.cpp` — call sites of `clearCaches()`

**Complexity:** Low  
**Impact:** Medium

---

### Phase 10 — Async Tooltip Fetch

**Goal:** Eliminate hover stutter from synchronous SQLite + file I/O.

`getTooltipSnippetForNode()` reads multiple SQLite tables and a source file
synchronously on the UI thread on every hover. Replace with a debounced
background fetch (e.g. 150ms delay, cancellable):

```cpp
// In TooltipController::handleMessage(MessageTooltipShow*)
m_pendingTooltip.cancel();
m_pendingTooltip = scheduleAfter(150ms, [tokenIds] {
    auto info = storageAccess->getTooltipInfoForTokenIds(tokenIds, origin);
    QMetaObject::invokeMethod(this, [info] { showTooltip(info); });
});
```

**Affected files:**

- `src/lib/component/controller/TooltipController.cpp`
- `src/lib/data/storage/PersistentStorage.cpp`

**Complexity:** Low  
**Impact:** Low–Medium (quality of life)

---

## Priority Summary

| # | Improvement | Complexity | Impact | Phase |
| --- | --- | --- | --- | --- |
| B1+B2 | Async data fetch off UI thread | High | **Critical** | 1 |
| B6 | Parallel graph + code fetch | Medium | **High** | 2 |
| B1+B2 | Batch SQLite queries in hot path | Medium | **High** | 3 |
| B3 | Fix `bundleNodes()` O(N²) scan | Low | **High** | 4 |
| B4 | Batch namespace lookups | Low | **High** | 5 |
| B5 | Parallel `buildCaches()` | Medium | **High** | 6 |
| B7 | Parallel `layoutNestingRecursive()` | Low | Medium | 7 |
| B8 | Fix `BucketLayouter` deque cycling | Medium | Medium | 8 |
| B9 | Smarter `m_graphForAll` invalidation | Low | Medium | 9 |
| B10 | Async tooltip fetch | Low | Low–Medium | 10 |

Phases 4 and 5 are pure algorithmic fixes with no threading risk — they should
be done first as they are low-risk and high-reward. Phase 1 (async fetch) is the
single highest-leverage change for perceived responsiveness but requires careful
cancellation logic to avoid showing stale results.

---

## Prerequisites

- **SQLite WAL mode** must be enabled for any multi-reader parallelism (Phases
  2, 3, 6). WAL mode allows concurrent reads without blocking writes.
  `PRAGMA journal_mode=WAL;` at database open time.
- **Per-thread SQLite connections** — SQLite connection objects are not
  thread-safe. Each background thread needs its own `SqliteIndexStorage`
  instance opened on the same database file.
- **C++17** with `<execution>` for Phase 7. Already likely available given the
  existing codebase style.
- **Cancellation tokens** for Phase 1 to discard results from superseded
  activations (rapid clicking through the graph).
