# Roadmap: Swift Language Indexer

## Goal

Bring `sourcetrail_swift_indexer` from a transport-only stub to full parity with
the C++/Rust indexing pipeline: a standalone macOS binary that indexes Swift
packages over the existing thoth-ipc / FlatBuffers protocol, with a **hybrid
engine** — semantic data from the compiler-produced index store (IndexStoreDB)
plus a SwiftSyntax declaration fallback for code that is not (yet) buildable.

The staged design and rationale live in
[DESIGN_SWIFT_INDEXER.md](DESIGN_SWIFT_INDEXER.md) (the SW0–SW9 series). This
file tracks status at a glance.

---

## Status (2026-07-17)

| Stage | What | State |
|-------|------|-------|
| SW0 | Build revival: flatc Swift codegen migration, Core library + test target, `BUILD_SWIFT_LANGUAGE_PACKAGE=ON` in macOS presets | ✅ done |
| SW1 | Package model (`swift package describe`) + build driver + diagnostics → non-fatal errors + per-file progress | ✅ done |
| SW2 | Semantic core: IndexStoreDB → nodes/edges/occurrences, USR→NameHierarchy resolution | ✅ done |
| SW3 | Syntactic fallback (SwiftSyntax) + hybrid merge (freshness rule) | ✅ done |
| SW4 | Robustness: chunked push, shared 200-failure retry budget | ✅ done |
| SW5 | Host-side per-package command emission; Rust `equalsSettings` bugfix | ✅ done (options fields deferred) |
| SW6 | K Swift supervisor fan-out | ✅ done |
| SW7 | Package-granular shard striping (Rust + Swift) | ✅ done |
| SW8 | GUI wizard for Swift options | ⏸ deferred (bundled with SW5 options) |
| SW9 | `index_self` smoke binary, docs | ✅ done |
| SW10 | Code-view parity: SCOPE locations + precise token extents via universal SwiftSyntax | ✅ done (local symbols / qualifiers follow-up) |

The engine is complete and exercised end to end. `swift run index_self` on the
indexer's own package produces ~30 files / ~2k nodes / ~7.8k edges / ~13k
occurrences (the occurrence jump over pre-SW10 is the added SCOPE locations).

---

## Deferred, as one bundle

The **command option fields** (`swift_build_args`, `swift_toolchain_path`,
`swift_index_store_path`), their **settings mixin**
(`SourceGroupSettingsWithSwiftOptions`), and the **GUI wizard** (SW8) are held
together until there is a consumer. Today `BuildDriver` runs `swift build` with
defaults and auto-discovers the index store, so those fields + tri-language IPC
pop-rewrites would be dead plumbing. Land them with the BuildDriver code that
reads them (custom toolchain selection, read-only-checkout index-store
override).

## Known limitations / follow-ups

- **Local symbols & qualifiers** (SW10 follow-up): function-local `let`/`var`
  navigation (`LOCATION_LOCAL_SYMBOL`) and clickable qualified-name segments
  (`LOCATION_QUALIFIER`) are still unemitted. Locals need `-index-include-locals`
  on the build (semantic) or a scoped SwiftSyntax name walk; both are additive
  on top of the SW10 `DeclScopeMap`/enrichment plumbing.
- **Token extents for references** are still approximated by base-identifier
  length (SW10 made *definition* name extents exact via SwiftSyntax; arbitrary
  reference tokens would need a full per-file token map — cheap to add if the
  approximation ever mis-highlights a real reference).
- **Actors** map to `NODE_CLASS` (no schema/GUI churn); revisit if a distinct
  actor node kind is ever wanted.
- **Degenerate sharding**: a shard whose file-level stripe is empty emits no
  package commands (more shards than files) — unchanged from the file-level
  behavior.
- **Optional**: a C++ ipc-harness integration test with Swift commands. The
  subprocess is already covered end to end by the package's own real-build
  integration tests, so this is low-value.

## Non-goals

- **Turso from the subprocess.** The app's concurrent Turso writer stays sole
  writer; the subprocess pushes IntermediateStorage over SHM. A Swift-side
  need would go through `turso_shim`'s `tsq_` C API, not a second Turso SDK.
- **Non-macOS.** The package pins `.macOS(.v14)` and uses `Darwin.POSIX` +
  IndexStoreDB's toolchain `libIndexStore.dylib`.
