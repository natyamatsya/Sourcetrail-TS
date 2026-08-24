# Storage schema — identity, tables, indices

- **Status:** reference, written 2026-08-24 against storage version **28**.
- **Source of truth:** `SqliteIndexStorage::setupTables()` and
  `SqliteIndexStorage::getIndices()`
  (`src/lib_core/data/storage/sqlite/SqliteIndexStorage.cpp`), plus `meta` from
  `SqliteStorage.cpp`. The DDL is duplicated in
  `src/lib_core/data/storage/sqlite/index.sql`, which generates `IndexTables.h` for
  sqlpp23 — **a schema change has to land in both**, as the comment above
  `setupTables()` says.
- **Do not read the schema off a `.srctrl.db` you happen to have.** Files migrate
  lazily and lag the code: the index committed in this repo predates the cross-language
  work and has no `node.languages` column. Read the source.
- **Related:** [DESIGN_TURSO_BACKEND.md](DESIGN_TURSO_BACKEND.md) (the alternate
  backend and where it deviates), [DESIGN_STORAGE_CODEGEN.md](DESIGN_STORAGE_CODEGEN.md)
  (facets and transport mirrors), [EXPERIMENT_LADYBUG_MIRROR.md](EXPERIMENT_LADYBUG_MIRROR.md)
  (traversal measurements over this schema).

## The identity spine

`element` is the whole design in one table:

```sql
CREATE TABLE element(id INTEGER, PRIMARY KEY(id));
```

It holds nothing but an id. Everything indexable — a node, an edge, a local symbol, an
error — *is* an element, and the specialising table reuses the element's id as its own
primary key rather than carrying a foreign key to it. So `node.id`, `edge.id` and
`element.id` are the same number, ids are unique across kinds, and `occurrence` can
reference "whatever element this location belongs to" without knowing which kind it is.

That is what makes `occurrence(element_id, source_location_id)` work as a single
table for every kind of thing that appears in source text.

Two levels of specialisation stack on it: `node` refines `element`, and `symbol`,
`file` and `component_access` refine `node`.

```
element ─┬─ node ─┬─ symbol
         │        ├─ file ── filecontent
         │        ├─ component_access
         │        └─ node_attribute
         ├─ edge   (source_node_id, target_node_id → node)
         ├─ local_symbol
         ├─ element_component
         └─ error
                     source_location → node (the file node)
                     occurrence  → element + source_location
```

## Tables

| Table | Key | References (all `ON DELETE CASCADE`) | Holds |
|---|---|---|---|
| `element` | `id` | — | identity only |
| `node` | `id` | `id` → `element` | `type`, `serialized_name`, `modifiers`, `languages` |
| `edge` | `id` | `id` → `element`; `source_node_id`, `target_node_id` → `node` | `type` |
| `symbol` | `id` | `id` → `node` | `definition_kind` |
| `file` | `id` | `id` → `node` | `path`, `language`, `modification_time`, `indexed`, `complete`, `line_count` |
| `filecontent` | `id` | `id` → `file` (also `ON UPDATE CASCADE`) | `content` |
| `local_symbol` | `id` | `id` → `element` | `name` |
| `source_location` | `id` | `file_node_id` → `node` | start/end line+column, `type` |
| `occurrence` | (`element_id`, `source_location_id`) | both | the join between elements and text |
| `component_access` | `node_id` | `node_id` → `node` | `type` |
| `node_attribute` | (`node_id`, `key`, `value`) | `node_id` → `node` | arbitrary per-node attributes |
| `element_component` | `id` | `element_id` → `element` | `type`, `data` |
| `error` | `id` | `id` → `element` | `message`, `fatal`, `indexed`, `translation_unit` |
| `file_command_hash` | `path` | **none** | `hash` — flag-aware refresh |
| `meta` | `key` | — | `storage_version`, `project_settings`, `timestamp` |

`file_command_hash` is the deliberate exception: keyed by path rather than node id so
the refresh generator can compare without joining, and added with `CREATE TABLE IF NOT
EXISTS` so old databases upgrade in place — an absent hash reads as "unknown", not as
"changed". `element_id_to_clear` is a scratch table built and dropped inside the clear
path, not part of the persistent schema.

## Indices are per-mode, and `setMode()` drops as well as creates

`getIndices()` returns `(mode mask, index)` pairs, and `setMode()` walks the whole list:
create the index if its mask matches the mode being entered, **drop it otherwise**. So
the index set is not cumulative — it is rebuilt to match the mode, and switching modes
deletes indices that do not belong to the new one.

| Mode | Entered by | Indices |
|---|---|---|
| `READ` | `Project.cpp:223` on load, `TaskFinishParsing.cpp:47` after a run | `edge(source_node_id)`, `edge(target_node_id)`, `node(serialized_name)`, `source_location(file_node_id)`, `occurrence(element_id)`, `occurrence(source_location_id)` |
| `WRITE` | `TaskParseWrapper.cpp:28`, `Project.cpp:1042` | `error(message, fatal)`, `file(path)` |
| `CLEAR` | `TaskCleanStorage.cpp:41` | everything in `READ`, plus `element_component(element_id)` |

The asymmetry is the point. Indexing runs in `WRITE` **without** the edge and occurrence
indices, so bulk inserts do not pay to maintain them; they are built once when the run
finishes and the storage flips to `READ`. On this repo's own index (40k nodes, 201k
edges) that rebuild costs about 0.3 s and roughly 8% of the file.

Until 2026-08-24 the edge indices were registered for `CLEAR` only, so entering `READ`
*deleted* them and every traversal in the running application scanned all 201k edges —
a depth-3 transitive-callee query took 47 ms instead of 1.3 ms. Fixed in `74249d97`.

## Foreign keys, cascades, and the index a cascade needs

Every reference above is `ON DELETE CASCADE`, and enforcement is real: `SqliteStorage::
enablePragmas()` sets `PRAGMA FOREIGN_KEYS=ON`. (The pragma is per-connection and
defaults off, so a `sqlite3` shell session reports `0` — that says nothing about the
application.) Deleting an `element` row therefore reaps its node or edge, the node reaps
its edges, occurrences and attributes, and so on down the spine. That is how
`TaskCleanStorage` removes a file's worth of index without hand-written deletes for
every table.

The cost sits on the **child** side, and this is the part that is easy to get wrong:

- The **referenced** column (`node.id`) must be a primary key or unique, so SQLite always
  has an index on it for free.
- The **referencing** column (`edge.source_node_id`) gets no index automatically — in
  SQLite or any other engine. But that is exactly the column a cascade must search:
  deleting a node asks *"which edges point at this?"*, and with no index that is a full
  scan of `edge` **per deleted node**.

Hence the familiar advice to *index your foreign keys*, meaning the referencing column,
and hence `element_component_foreign_key_index` on `element_component(element_id)` —
nothing queries that column, only the cascade walks it, so the index exists purely to
make deletion cheap.

### The duplicate trap that pattern creates

An index carries no purpose. SQLite stores a b-tree over a column list and has no notion
of "this one is for referential integrity, that one is for queries" — the cascade
machinery and the query planner both simply look for a usable index on the column.

So when an index-your-foreign-keys pass runs over a schema that already has
query indices on the same columns, the result is two identical b-trees per column set,
both maintained on every write, one of them never consulted. Five such pairs had
accumulated here — every `*_foreign_key_index` except `element_component`'s duplicated a
`*_id_index` exactly. Removed in `513ac13c`.

Two things follow for anyone touching `getIndices()`:

- **Compare by `(table, column list)`, not by name.** The duplicates had distinct names
  and distinct mode masks, which made them look like distinct mechanisms. Nothing in the
  code checks for overlap.
- **Redundancy includes prefixes.** An index on `(a)` is redundant when `(a, b)` exists,
  since a b-tree on `(a, b)` also serves lookups on `a`. Leading order decides it:
  `(type, source_node_id)` would *not* subsume `(source_node_id)`.

Duplicate indices are invisible on reads — the planner picks one and ignores the rest —
and bill only writes and disk. That is precisely the profile of a defect that survives
for years in a codebase whose write path is a batch run nobody profiles per statement.

### Removing an index needs care

`setMode()` can only drop what it still knows about. Deleting an entry from
`getIndices()` outright leaves the index in place in every database that already has it,
where SQLite goes on maintaining it forever with nothing to ever remove it. The
retirement path is to keep the entry with a mode mask of `0`, which never matches, so
`setMode()` always takes its drop branch; the lines can be deleted once no database in
circulation predates the change.

## Versioning

`SqliteIndexStorage::s_storageVersion` (currently 28) is compared against the value in
`meta`. Additive changes that old files tolerate — a new `CREATE TABLE IF NOT EXISTS`, a
column read as a default when absent — can land without a bump. Anything that would make
an old file misread needs one, and a bump forces a re-index.
