# Out-of-process C++20 module prebuild

## Motivation

Indexing C++20 modules needs a *prebuild* step before the normal per-TU indexing:
scan the source group for module `provides`/`requires`, order the interface units,
and build their BMIs (`.pcm`) so `import`s resolve. Three properties matter, and the
current implementation only has two of them at a time:

1. **Crash isolation** — scanning/building parses arbitrary user C++, which can
   segfault, assert, hang or OOM inside libclang. Sourcetrail already isolates *indexing*
   in the `sourcetrail_indexer` subprocess for exactly this reason.
2. **Config match** — the BMI must be built with the same libclang configuration the
   indexer later loads it with, or it's rejected (the target-triple / builtin-header
   hazard we hit in Phase 2).
3. **No external binaries** — not depending on a `clang-scan-deps` / `clang++` install.

| Approach | isolation | config match | no external bin |
|---|---|---|---|
| Phase 2: shell out to `clang++`/`clang-scan-deps` | ✅ (separate proc) | ❌ (had to pin `-target`) | ❌ |
| Phase 3: in-process in the **main** app process | ❌ (a bad TU kills the app) | ✅ | ✅ (BMI) / ❌ (scan) |
| **This design: in-process in a `sourcetrail_indexer` subprocess** | ✅ | ✅ | ✅ |

Phase 3 (`CxxModulePrebuilder` running in `SourceGroupCxxEmpty::getIndexerCommandProvider`)
parses in the main process — same crash-blast-radius weakness as the existing PCH build
(a `TaskLambda` running `GeneratePCHAction` in-app). This design moves the parsing into a
subprocess and gets all three properties at once.

## Target architecture

`sourcetrail_indexer` gains a second mode. Today it only connects to the shared-memory
IPC and runs `InterprocessIndexer::work()`. With `--prebuild-modules=<request.json>` it
instead runs the prebuild and exits. The main process spawns it exactly the way it already
spawns indexer workers — `utility::executeProcess(getCxxIndexerFilePath(), args…)`
(TaskBuildIndex.cpp) — which blocks on exit and surfaces crashes as a non-zero code.

```
main process (SourceGroupCxxEmpty::getIndexerCommandProvider)
  │  cheap text pre-filter (file reads only, no parsing)  ── no modules ──▶ index as today
  │  modules present:
  │    write request.json { cacheDir, sourceFiles[], baseFlags[] }
  │    executeProcess(sourcetrail_indexer, [std args…, --prebuild-modules=request.json])  ◀─ BLOCKS = barrier
  │                       │  (separate process = crash firewall)
  │                       ▼  indexer main.cpp sees the flag → runModulePrebuild()
  │                          scan (provides/requires) → Kahn's topo → buildBmiInProcess() per unit
  │                          → cacheDir/<name>.pcm  +  cacheDir/manifest.json { interfaceUnits[] }
  │                          exit(0 | non-zero)
  │    exit != 0 → log + index WITHOUT module resolution (graceful, as today)
  │    exit == 0 → read manifest.json
  ▼    inject -fprebuilt-module-path=<cacheDir> into every CXX command,
       -x c++-module into interface units, emit IndexerCommandCxx as today
```

Channels are the filesystem: `request.json` (main→sub), `*.pcm` + `manifest.json`
(sub→main). No change to the shared-memory command/storage protocol. The barrier is just
the blocking spawn.

### Data contracts

- `request.json`  — `{ "cacheDir": str, "sourceFiles": [str], "baseFlags": [str] }`
- `manifest.json` — `{ "interfaceUnits": [str] }` (the module path is `cacheDir`, which the
  main process already knows, so it isn't echoed back)

## Phases

### Phase A — the subprocess mode (keeps clang-scan-deps)
- `indexer/main.cpp`: parse `--prebuild-modules=<path>`; when present call
  `CxxModulePrebuildRunner::run(path)` after the existing AppPath/UserPaths setup, and
  return — do **not** construct `InterprocessIndexer`.
- New `CxxModulePrebuildRunner` (lib_cxx): the moved body of today's
  `CxxModulePrebuilder::prebuild` — scan (clang-scan-deps, unchanged for now) → topo →
  `buildBmiInProcess` (unchanged) → write `manifest.json`. Reads `request.json`.
- **Verify:** hand-write a `request.json`, run `sourcetrail_indexer --prebuild-modules=…`,
  assert `*.pcm` + `manifest.json` are produced.

### Phase B — main-side integration (achieves crash isolation)
- `CxxModulePrebuilder::prebuild` becomes the thin main side: cheap detect
  (`mightUseModules`, stays in main — file reads only) → write `request.json` → spawn the
  indexer in prebuild mode + wait → read `manifest.json` → return `Result{sharedFlags,
  interfaceUnits}`. The scan/topo/BMI logic is gone from here (moved to the runner).
- `SourceGroupCxxEmpty::getIndexerCommandProvider` is unchanged in shape — it still calls
  `CxxModulePrebuilder::prebuild` and injects the returned flags.
- **Verify:** modules_demo indexes E2E (prebuild now out-of-process); tictactoe unchanged
  (no spawn); a deliberately-crashing module unit degrades gracefully (app survives, error
  logged) — the property this whole design exists for.

### Phase C — in-process scan (retires the last external binary) — DONE
- Replaced the `clang-scan-deps` shell-out in the runner with a `clang::Lexer` raw token pass
  (`scanModuleDeps`): `export module X` → provides X; `import X` / `export import X` →
  requires X; header-unit imports (`import <h>` / `import "h"`) skipped; dotted names and
  `:partition`s parsed. Robust to comments/strings (the lexer handles them); the known gap is
  module decls hidden behind macros / `#if`, which is rare.
  - `clang::tooling::dependencies::DependencyScanningTool` would be the "real" scanner but is
    **blocked** in this env — the `libclangDependencyScanning.a` lib links but the headers
    aren't shipped. If they appear, swap `scanModuleDeps` for it; the interface is the same.
- **Verified:** modules_demo indexes with `env -i PATH=/usr/bin:/bin` (no clang tools at all) —
  0 errors, module/import/export recorded; a dotted-name dependency chain (`app.a`←`app.b`)
  topo-orders and builds both BMIs.

### Phase D — PCH build moved out-of-process — DONE
- The PCH build (`createBuildPchTaskForInput`) parsed arbitrary user code in the main process
  via a `TaskLambda` -- the same in-app-parsing risk the module prebuild had. Moved it into a
  `--prebuild-pch=<request>` subprocess mode (`CxxPchBuildRunner`), mirroring the module
  prebuild. The wrinkle vs modules: the PCH build also *indexes symbols*, so the subprocess
  serializes its `IntermediateStorage` (via `IpcSerializer::serializeIntermediateStorage`) to
  `<pchOutput>.storage`; the main side deserializes and `storageProvider->insert`s it. The
  `.pch` artifact itself is a plain file, as before.
- **Verified behavior-preserving:** a PCH project (`pch_demo`) indexed before (in-main) and
  after (out-of-process) produces a byte-identical graph (node and edge dumps `diff`-clean),
  0 errors. modules_demo / tictactoe / 284 unit cases unaffected.

### Phase E — compile-database source group — DONE
- Extended module support to `SourceGroupCxxCdb`. Two things it needed vs the plain C++ Source
  Group:
  - **Per-file flags.** `CxxModulePrebuilder::prebuild` now takes a `map<FilePath, flags>` (each
    compile-database command carries its own flags); the Empty group just maps every file to the
    same set. A module-interface unit's BMI is built with that file's own flags. The runner also
    normalizes a CDB command (drops the `argv[0]` compiler, `-c`, `-o <out>`, `-x <lang>`, and the
    input file) before adding its BMI-generation flags -- mirroring `CxxParser::buildIndex`.
  - **Guard.** If any compile command already carries `-fmodule-file=` / `-fprebuilt-module-path=`,
    the build system is modules-aware and resolves imports itself, so the prebuild is skipped.
- **Verified:** a compile-database project (`geo.cpp` module + `main.cpp` importer, no module
  flags in the commands) indexes 0 errors with the `geo` module node, `global -> geo` import edge,
  and `geo.pcm` built. A CDB that already carries `-fprebuilt-module-path` skips the prebuild (no
  cache). modules_demo / tictactoe / PCH / 284+6 unit cases unaffected.

## Risks / notes
- One extra process spawn per index of a module project (one-time, not per-TU), plus the
  JSON round-trip. `getIndexerCommandProvider` blocks on it — fine, it's a pre-index step,
  like the PCH build already is.
- The prebuild process needs the same AppPath/ResourcePaths setup as the worker (for
  `getCommandlineArgumentsEssential` / the bundled builtin headers), so it takes the same
  `sharedDataPath` / `userDataPath` positionals; it just skips the IPC connection.
