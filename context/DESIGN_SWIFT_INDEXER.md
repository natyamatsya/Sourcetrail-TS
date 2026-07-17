# Design: Swift Indexer Parity (SW series)

**Status: SW0–SW5 implemented (2026-07-17) — build revived, transport tested,
package model + build driver landed, semantic core (IndexStoreDB) emitting
nodes/edges/occurrences, syntactic fallback (SwiftSyntax) + hybrid merge,
robustness parity (chunking, retry budget), host-side per-package command
emission; SW6–SW9 planned.**
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
- **SW6 — Fan-out.** `swiftSupervisorCount = min(distinct package roots, 3)`
  under the tri-state gate; storage-manager vector; generalized
  `swiftIndexerProcessId = processCount + rustSupervisorCount + 1 + k`.
- **SW7 — Package-granular shard striping (Rust + Swift).** Deterministic
  `pos % N == i-1` stripe over the sorted package-root set when a ShardConfig
  is active; extend `ShardConfigTestSuite` + `scripts/smoke-distributed.sh`.
- **SW8 — GUI wizard.** Swift options content cloned from the Cargo one.
- **SW9 — Close-out.** C++ ipc integration test with Swift commands,
  `index_self`-style smoke binary, ROADMAP_SWIFT_INDEXER.md, TOPIC_MAP.

Sequencing: SW0→SW1→SW2→SW3→SW4; SW5 independent after SW0; SW6 needs SW5;
SW7 after SW5; SW8/SW9 last.
