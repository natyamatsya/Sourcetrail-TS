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

- `request.json`  — `{ "cacheDir": str, "files": [ { "path": str, "flags": [str] } ] }` (per-file
  flags: each compile-database command carries its own; the plain C++ Source Group maps every file
  to the same set)
- `manifest.json` — `{ "interfaceUnits": [str] }` (the module path is `cacheDir`, which the
  main process already knows, so it isn't echoed back)

### Known limitation: module partitions
Partition dependencies are tracked (`export module foo:part;` provides `foo:part`; `import :part;`
resolves against the current module), so topo order is correct. But the on-disk BMI name sanitizes
`:` to `-` (a `:` is illegal in a Windows path), and whether `-fprebuilt-module-path` resolves a
partition BMI under that name is not verified — partition support is best-effort until exercised by
the planned stress test.

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

### Phase F — standard library module (`import std;`) — DONE
- A source group can `import std;` (and `import std.compat;`) even though the module lives in the
  toolchain, not the source group. After the scan, any required module with no in-group provider that
  is `std` / `std.compat` is built from the toolchain's shipped `std.cppm` / `std.compat.cppm` into
  the cache, **before** the source BMIs (a source module may itself `import std;`). std before
  std.compat (which imports std).
- **Locating `std.cppm`.** It ships at `<llvm-prefix>/share/libc++/v1/std.cppm`. To compile a BMI the
  in-process libclang can load, it must be the `std.cppm` matching that libc++ — so we anchor on the
  LLVM install prefix the app is *linked* against, baked in as `SOURCETRAIL_LLVM_INSTALL_PREFIX`
  (`src/lib_cxx/CMakeLists.txt`, from CMake's `LLVM_INSTALL_PREFIX`); a resolvable compiler's resource
  dir is a fallback.
- **Two toolchain quirks the std build needs** (both learned the hard way):
  - **Sysroot / libc++ path.** The std BMI is built with the flags of a file that imports std, so it
    inherits the group's `-isysroot` and libc++ include paths. Minimal flags fail with
    `'__config' file not found` (libc++'s internal headers aren't on the default search path).
  - **INFINITY / NAN shim.** Xcode 27's SDK cleanup dropped the `INFINITY` / `NAN` / `HUGE_VAL*` C
    math macros from the headers `<complex>`/`<cmath>` pull in, so `std.cppm` fails with
    `use of undeclared identifier 'INFINITY'`. The runner writes a tiny `-include` shim
    (`std_math_shim.h`) defining the standard builtins if absent. Plus
    `-Wno-reserved-module-identifier` for std.cppm's reserved names.
- The std modules are **not** added to the manifest's `interfaceUnits` (they aren't source-group
  files the main process indexes); the built `std.pcm` just sits in the cache for
  `-fprebuilt-module-path` resolution. The existing main-side logic already adds
  `-fprebuilt-module-path=<cache>` and sets `anyModules` whenever the subprocess succeeds, so a
  std-only project (no source modules) is covered.
- **Verified:** `import std;`-only project indexes **0 errors** (was 1 fatal "module 'std' not
  found"), with the IMPORT edge and std's own symbols (`std/string.inc`, …) in the graph. A source
  module that itself `import std;`, imported by a `main` that also imports std, indexes 0 errors with
  std.pcm + the module's BMI both built and 3 import edges. modules_demo / modchain unaffected.

### Pre-filter precision (whole-word)
The cheap `mightUseModules` pre-filter (decides whether to spawn the prebuild at all) matches
`module` / `import` as **whole words**, not substrings — a substring scan fired on the very common
word `important` (and identifiers like `module_count`), needlessly spawning a no-op prebuild for
non-module source groups. Whole-word matching has no false negatives (a real module unit always has
`module`/`import` as a keyword). Surfaced while stress-testing against a large non-module codebase.

### Diagnostics (surfacing BMI build failures)
The prebuild runs in a subprocess launched **without a log-file path**, so its `LOG_*` calls go
nowhere and, worse, a failed BMI build's clang diagnostics used to vanish to the subprocess's stderr
(which the app discards) — a failure showed up only as a downstream "module not found", debuggable
only by re-running the prebuild by hand. Now:
- `buildBmiInProcess` installs a `clang::TextDiagnosticPrinter` over a string and returns
  `std::expected<void, std::string>` — on failure the error **is** the captured clang diagnostics
  (with source context and notes).
- The runner collects per-module failures (`{module, file, diagnostics}`), prints them to stderr (for
  a direct run) **and** writes them to `manifest.json` under `failures` — a channel independent of the
  exit code (the runner returns 0 even on a partial failure).
- The main-side `CxxModulePrebuilder` reads `manifest.failures` and `LOG_ERROR`s each, so the real
  cause lands in the **normal index log**. Successful builds stay quiet (warnings on a good build are
  not surfaced) — quiet on success, full diagnostics on failure.

### Stress test: Vulkan-Hpp (the installed SDK's `vulkan.cppm`) — PASSED
The largest real module we could find: the Vulkan SDK's generated `vulkan.cppm` (**10,675 lines** —
the whole Vulkan API). It exercises three axes at once: a **module partition** (`export import :video;`,
provided by `vulkan_video.cppm`), **`export import std;`**, and pervasive `#if`-guarded platform code.
Result: **2/2 files, 0 errors, 6,837 symbols**; three BMIs built in dependency order — `std.pcm`
(33 MB), the `vulkan:video` partition (`vulkan-video.pcm`, 32 MB — the `:`→`-` filename sanitization
in action), and `vulkan.pcm` (62 MB); ~67 s wall, ~980 MB peak RSS. `std::hash<vk::…>` specializations
resolve through both the vulkan and std modules.

Two real-world snags it surfaced (both handled outside the indexer, as a user's project config would):
- **Xcode 27 SDK header hygiene** — building `vulkan.hpp` as a BMI fails with `'getenv' must be
  declared before it is used` (the same class as the INFINITY/NAN std-module issue): the strict module
  build no longer sees the transitive `<cstdlib>`. Fixed by adding `#include <cstdlib>` to the module's
  global fragment. This is exactly the failure the new diagnostics pipeline now makes visible in the
  index log.
- **SDK partition-name inconsistency** — the shipped `vulkan.cppm` (module `vulkan`) imports `:video`,
  but `vulkan_video.cppm` declares `export module vulkan_hpp:video;` (mismatched base name, a mid-rename
  packaging quirk in this SDK snapshot). Made consistent for the test.

## Risks / notes
- One extra process spawn per index of a module project (one-time, not per-TU), plus the
  JSON round-trip. `getIndexerCommandProvider` blocks on it — fine, it's a pre-index step,
  like the PCH build already is.
- The prebuild process needs the same AppPath/ResourcePaths setup as the worker (for
  `getCommandlineArgumentsEssential` / the bundled builtin headers), so it takes the same
  `sharedDataPath` / `userDataPath` positionals; it just skips the IPC connection.
