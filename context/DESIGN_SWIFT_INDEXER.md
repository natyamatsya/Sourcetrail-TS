# Design: Swift Indexer Parity (SW series)

**Status: SW0–SW7 + SW9 + SW10 implemented (2026-07-17). The engine is complete
and exercised end to end, and now reaches code-view parity (SCOPE locations +
precise definition-name extents). SW8 (GUI) is bundled with the deferred SW5
options fields; local symbols / qualifiers are the SW10 follow-up.**
The Swift analog of the Rust indexer track: take the transport-complete but
analysis-empty Swift subprocess (`src/swift_indexer`) to full parity with the
C++/Rust pipeline, staged like [DESIGN_MULTIGROUP_FANOUT.md](DESIGN_MULTIGROUP_FANOUT.md)
(S0–S5) and [DESIGN_RUST_CRATE_FANOUT.md](DESIGN_RUST_CRATE_FANOUT.md) (R1–R4).

Starting point (verified 2026-07-17): the subprocess speaks all three SHM
channels correctly — command pop with lossless queue rewrite, status with
crash bookkeeping, storage push with back-pressure — but pushed an **empty
IntermediateStorage** for every command. No parser of any kind. Not compiled
in dev builds; the checked-in channel code predated the current flatc Swift
codegen API. Host side: one command, no options, no SPM discovery, one
supervisor with zero failure tolerance, no fan-out participation.

## Engine decision: hybrid — IndexStoreDB primary, SwiftSyntax fallback

Requirement: **indexing must still work on broken or partially built code**
(incremental broken builds). Neither engine alone satisfies this:
IndexStoreDB needs compiled index units; SwiftSyntax alone has no semantics.

Per command (= one SPM package root):

1. **Project model** — `swift package describe --type json` (60 s timeout) →
   targets, module names, file→module map. Failure ⇒ all indexed `.swift`
   files become one module named after the directory (mirrors the cargo-failure
   fallback in `SourceGroupRust.cpp`).
2. **Build** — `swift build` with index-store enabled, reusing the package's
   own `.build` (incrementality + stale units that survive a broken build).
   Compiler diagnostics → non-fatal `StorageError` rows, like Rust panics.
3. **Semantic pass** — IndexStoreDB over the store. Unit freshness = unit
   mtime ≥ source mtime. Occurrences → StorageOccurrence/SourceLocation; USR
   table → NameHierarchy chains → serialized names (same `"::\tm"` wire
   encoding as `collector.rs`). Relations → CALL / OVERRIDE / INHERITANCE
   (class + conformance) / MEMBER / TYPE_USAGE / USAGE / IMPORT edges.
4. **Coverage set** — files with fresh units are semantic, `complete=true`.
5. **Syntactic fallback** — SwiftSyntax decl walk for every indexed file NOT
   covered: nodes, MEMBER edges from nesting, definition occurrences only;
   `complete=false` so refresh upgrades the file once the build heals; one
   non-fatal StorageError per fallback file.
6. **Merge rule** — per file strictly exclusive (semantic XOR syntactic).
   **Stale semantic loses to syntactic**: stale occurrence offsets are wrong
   after edits; `complete=false` guarantees the upgrade later. Cross-file node
   unification happens at PersistentStorage inject via serialized-name dedup.

**Where the fallback actually engages (measured, not assumed).** swiftc's
index-while-building emits a per-file index unit even when the *overall* build
fails, as long as that file type-checks — so a **type error** (`let x =
doesNotExist`) still yields fresh, exact semantic data for its file. The
syntactic fallback therefore fires in a narrower, real set of cases: a **parse
error** severe enough that swiftc bails before writing units (the whole
module's units go stale), a file **not yet built**, or any file whose newest
unit predates its source. This is a strictly better degradation curve than
"broken build ⇒ syntactic": most broken states keep full semantics.

Kind mapping: struct→STRUCT, class/actor→CLASS, enum→ENUM, case→ENUM_CONSTANT,
protocol→INTERFACE, typealias/associatedtype→TYPEDEF, global func→FUNCTION,
method/init/deinit/subscript→METHOD, property→FIELD, global var→GLOBAL_VARIABLE,
generic param→TYPE_PARAMETER, module→MODULE, macro→MACRO. Extensions get no
node of their own — members attach to the extended type.

Dependencies: `indexstore-db` + `swift-syntax` as SwiftPM URL deps **pinned by
exact revision** matching the minimum toolchain (Swift 6.0). Apache-2.0 + RLE.
indexstore-db dlopens the toolchain's `libIndexStore.dylib` — fine for a
macOS-only indexer (`.macOS(.v14)`).

**Non-goal — Turso from the subprocess.** The app's concurrent Turso writer
stays sole writer (S4); the subprocess pushes IntermediateStorage over SHM.
If a Swift-side need ever arises, the route is C interop against
`turso_shim`'s `tsq_`-prefixed API (same pinned turso_core, no symbol
collisions) — recorded here so nobody reaches for a second Turso SDK.

**Build test targets too.** `BuildDriver` runs `swift build --build-tests`.
Plain `swift build` compiles only the library/executable products, so test
targets get no index units and fall to the syntactic pass — and in real
library packages the tests are a large share of the code. Measured on
apple/swift-syntax: without `--build-tests`, 299/544 files were semantic (all
245 test files syntactic); with it, 544/544. Users navigate test code as much
as source, so the extra build time is worth it.

## Validation against real projects (2026-07-17)

`swift run index_self <path>` was run against four public packages. All four
indexed with **zero crashes and 100% semantic file coverage** (after
`--build-tests`):

| Project | files | nodes | edges | occurrences | note |
|---------|-------|-------|-------|-------------|------|
| swift-algorithms | 56 | 1.9k | 8.9k | 15k | pure-generics baseline |
| swift-composable-architecture | 118 | 7.6k | 33k | 49k | macro-heavy (@Reducer, @ObservableState) |
| swift-nio | 479 | 23k | 153k | 233k | 65 modules, **84k cross-module edges** |
| swift-syntax | 544 | 35k | 193k | 263k | generated code + wide protocol trees |

Two properties confirmed with data (via `index_self` module stats + `--grep`):
- **Cross-module resolution** (NIO): 84,237 of 153k edges cross module
  boundaries (NIOPosix→NIOCore, NIOHTTP1→NIOCore, tests→their targets). USRs
  resolve across the whole store, so multi-module packages link correctly.
  The `<unknown>` module bucket (~1.5% of nodes) is the expected fallback for
  extensions of external/stdlib types.
- **Post-expansion macro structure** (TCA): `@ObservableState`-synthesized
  members are real nodes under the right parent —
  `ChildState::_$observationRegistrar`, `_$id`, `_$willModify()` and their
  accessors (93 registrar nodes, 84 `_$id` nodes). IndexStoreDB indexes after
  macro expansion, so the meaningful post-expansion graph is captured.

**Distributed indexing (P4b) stays intact**: shard producers are whole
processes with private DBs; this work is orthogonal. SW7 *improves* shard runs
by striping package-granular commands (today every shard re-indexes every
crate/package; merge dedup keeps it correct but wastes work).

## Stages

- **SW0 — Revival (DONE 2026-07-17).** Channel code migrated to the current
  flatc Swift API (`FlatbufferVector` collections replaced the `xCount` +
  `x(at:)` accessors). Package restructured: `SourcetrailSwiftIndexerCore`
  library + thin executable + `swift test` target with pop-rewrite
  losslessness tests (every schema field incl. `source_group_id`,
  `restrict_to_package`) and storage-framing tests. FlatBuffers now resolved
  from the libipc-vendored checkout (identity clash with the URL dep, and the
  runtime must match what LibIPC builds against). `BUILD_SWIFT_LANGUAGE_PACKAGE`
  ON in the macOS toolchain presets (`apple-clang-*`, `llvm-clang-*`).
- **SW1 — Package model + build driver (DONE 2026-07-17).**
  `PackageModel.swift` (`swift package describe --type json`, 60 s timeout,
  synthetic-single-module fallback), `BuildDriver.swift` (`swift build
  --enable-index-store`, `path:line:col: severity:` diagnostic parsing →
  non-fatal StorageErrors), `updateIndexing` per-file progress (no crash
  bookkeeping, analog of `status.rs`), StorageFile rows (`complete=false`
  until the engine lands, so refresh upgrades them). Verified end-to-end by
  an integration test that builds a broken fixture package.
- **SW2 — Semantic core (DONE 2026-07-17).** `indexstore-db` pinned by
  revision (`c993f4fb`, Swift 6.4). `StorageBuilder` ports the collector's
  dedup discipline (nodes by serialized name with placeholder-kind upgrade,
  edges by type+endpoints, files by path); `SemanticIndexer` walks
  `symbolOccurrences(inFilePath:)` per covered file — definitions emit
  node+symbol+member-edge (+override edges from relations), references emit
  call/inheritance/type-usage/usage/import edges with the containing symbol
  resolved from calledBy/containedBy/baseOf relations. USR→hierarchy resolves
  parents recursively via the childOf relation on the parent's canonical
  definition, memoized, with extensions redirected to the extended type via
  `occurrences(relatedToUSR:.extendedBy)` and unresolvable parents degrading
  to module-qualified names. Coverage = `dateOfLatestUnitFor(filePath:)` ≥
  source mtime, so unchanged files keep semantic data across a broken build.
  Token extents approximate: the store has no end column; the base identifier
  length is used. Verified by a golden fixture test (class/protocol/
  inheritance/conformance/call/member + occurrence locations).
- **SW3 — Syntactic fallback + merge (DONE 2026-07-17).** `swift-syntax`
  pinned to 602.0.0. `SyntacticIndexer` walks the parse tree (error-tolerant,
  so partial files still yield declarations) emitting nodes + member edges +
  definition occurrences with `complete=false`; a per-file fallback error row
  names the file. Function/init/subscript names carry parameter labels
  (`added(label:)`, `init(x:)`) to match the index-store display names exactly.
  Extension members attach to the module-qualified extended type. Merge is in
  `PackageIndexer`: semantic pass over covered files (unit ≥ source mtime),
  syntactic pass over the rest — strictly exclusive per file. `BuildDriver`'s
  store-path resolution was also corrected to walk `.build` for the real
  `index/store` (the toolchain uses the SwiftBuild layout
  `.build/out/Products/<Config>/index/store`, not the native
  `.build/<triple>/<config>` path). Verified: a golden test breaks a file with
  a parse error, sees the unchanged file stay semantic while the broken one
  degrades to syntactic declarations, then upgrades back on fix; a second test
  asserts syntactic name spellings are a subset of the semantic ones (no
  forked nodes).
- **SW4 — Robustness (DONE 2026-07-17).** `StorageChunker` ports the Rust
  chunker (`storage.rs`): a storage over a 7 MiB budget splits into
  self-contained chunks (every edge endpoint, occurrence element/location,
  and location file-node travels in the same chunk), pushed with
  back-pressure between chunks — one queue entry never outgrows the fixed
  16 MiB segment (ADR-0002). `TaskBuildIndex`'s Rust and Swift babysitters
  were merged into one `runExternalIndexerProcess(path, commandType, name)`,
  giving Swift the Rust supervisor's 200-consecutive-failure budget: a single
  subprocess crash restarts instead of aborting the whole run.
  **Deliberately skipped**: result caching (the Rust cache existed because
  many file-level commands mapped to one crate; Swift emits one command per
  package, so a `(workingDirectory, buildOptions)` cache would never hit —
  revisit only if SW5 ever emits multiple commands per package root) and the
  exit-on-empty-pop change (Swift's poll-until-`queueStopped` loop is correct
  and avoids re-running process init / rebuild churn on each requeue; the
  Rust exit-on-empty is paired with cheap relaunch, a tradeoff that does not
  favor the heavier Swift subprocess).
- **SW5 — Host-side project model (partial, 2026-07-17).** `SourceGroupSwift`
  now emits **one command per SPM package root**, found by a recursive
  `Package.swift` filesystem scan of the indexed paths (no subprocess needed,
  unlike cargo metadata); when no manifest is found it falls back to a single
  whole-directory command that the subprocess degrades to a synthetic
  single-module scan. This fixes multi-package repos (each `swift build` runs
  where a manifest exists) and is the granularity SW6 fans out over. Also
  fixed the latent `SourceGroupSettingsRustEmpty::equalsSettings` bug (it
  omitted `WithCargoOptions::equals`, so cargo-option-only edits were not
  detected as changed settings).
  **Deferred until a consumer exists**: the `swift_build_args` /
  `swift_toolchain_path` / `swift_index_store_path` command fields, the
  `SourceGroupSettingsWithSwiftOptions` mixin, and the GUI wizard (SW8). The
  subprocess's `BuildDriver` currently runs `swift build` with defaults and
  auto-discovers the store; adding schema fields + tri-language pop-rewrites
  that nothing reads would be dead plumbing. Land them together with the
  BuildDriver code that consumes them (custom toolchain / read-only-checkout
  index-store override).
- **SW6 — Fan-out (DONE 2026-07-17).** `Project::buildIndex` counts distinct
  Swift package roots (`swiftPackageCount`) exactly as it counts Rust crate
  roots, and sets `swiftSupervisorCount = min(count, 3)` under the same
  tri-state fan-out gate. `TaskBuildIndex` spawns that many Swift supervisor
  threads, each with its own `IntermediateStorageManagerImpl`
  (`m_swiftStorageManagers` vector replaced the single manager); the fetch
  loop iterates the vector. Process ids stay contiguous and non-overlapping:
  C++ `1..processCount`, Rust `+1..+rustSupervisorCount`, Swift
  `+rustSupervisorCount+1..+swiftSupervisorCount` via the generalized
  `swiftIndexerProcessId(processCount, rustSupervisorCount, k)`. The Rust and
  Swift babysitters already share `runExternalIndexerProcess` (SW4), so each
  Swift supervisor also has the 200-failure retry budget.
- **SW7 — Package-granular shard striping (DONE 2026-07-17).** The file-level
  `stripeFilter` (P4b) never reached Rust/Swift commands — they gate only on
  `filesToIndex` emptiness, so every shard re-indexed every package and
  distributed runs gave no speedup for these languages. New
  `shard::stripeKeys` applies the same `pos % N == i-1` rule to the sorted
  package/crate-root set; `Project::buildIndex` filters each language's
  commands through it (keyed on `getSourceFilePath()`, which equals the
  package root) and shrinks the root count so the SW6 supervisor count
  reflects the striped work. Verified by `ShardConfigTestSuite` (disjoint +
  complete + deterministic + balanced over package keys). Note: a shard whose
  file-level stripe is empty still emits no package commands — a degenerate
  more-shards-than-files case, unchanged from before.
- **SW8 — GUI wizard (DEFERRED, bundled with SW5 options).** A wizard would
  configure exactly the `swift_build_args` / `swift_toolchain_path` /
  `swift_index_store_path` options deferred in SW5 — there is nothing to
  configure until those fields exist. Land the wizard together with the
  option fields and the BuildDriver code that consumes them.
- **SW9 — Close-out (DONE 2026-07-17).** `index_self` executable target
  (`swift run index_self [path]`) runs the full hybrid pipeline on a package
  and prints a summary — the Swift analog of the Rust smoke binary, and a live
  demonstration of the hybrid split (semantic for built files, syntactic for
  the test targets). ROADMAP + this doc + TOPIC_MAP updated. The Swift
  subprocess is already exercised end to end by the package's own test target
  (real `swift build` + IndexStoreDB + SwiftSyntax across the SW1/SW2/SW3
  integration tests), which is stronger coverage than a C++ ipc-harness stub;
  a dedicated C++ ipc integration test is left as optional follow-up.

- **SW10 — Code-view parity (DONE 2026-07-17).** The graph (nodes + edges)
  reached parity at SW2; the code-view *experience* did not, because both
  passes emitted only a single approximate `TOKEN` location per symbol. The C++
  code view is rich because clang emits a wider location vocabulary; even the
  Rust indexer emits `TOKEN` + `SCOPE` + `LOCAL_SYMBOL`. SW10 closes the two
  highest-value gaps:
  - **`SCOPE` locations** — the brace-to-brace extent of every definition, so a
    whole class/function reads as one navigable, collapsible region and "show
    definition" lands on the body (not just the name line).
  - **Precise definition-name `TOKEN` extents** — exact byte ranges, replacing
    the identifier-*character*-count approximation (wrong for multi-byte /
    operator / backtick names).

  Both are *syntactic* facts IndexStoreDB (a semantic occurrence index) doesn't
  carry, so SW10 promotes **SwiftSyntax from fallback-only to a universal
  enrichment layer**. `SyntaxDecls.swift` (`DeclScopeMap`) parses each file once
  and maps a declaration's name-token position → (exact name extent, scope
  extent), keyed by the `(line, utf8-column)` coordinate IndexStoreDB also uses.
  The semantic pass looks up each definition and emits `TOKEN` + `SCOPE` (falling
  back to the approximate token on a miss, e.g. macro-synthesized members with
  no source decl); the syntactic pass emits the same pair directly from the tree.
  Verified on both engines (a deterministic syntactic fixture asserting exact
  extents + multi-line scope, and a semantic package build). **Follow-up**: local
  symbols (`LOCATION_LOCAL_SYMBOL`) and qualifiers (`LOCATION_QUALIFIER`), both
  additive on the `DeclScopeMap` plumbing — see ROADMAP.

Sequencing: SW0→SW1→SW2→SW3→SW4; SW5 independent after SW0; SW6 needs SW5;
SW7 after SW5; SW8/SW9 last; SW10 (code-view parity) builds on SW2/SW3.
