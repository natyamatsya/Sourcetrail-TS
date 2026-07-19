# Migrating the indexer to C++20 modules (flag-gated dual-build)

Roadmap for building Sourcetrail's own C++ **as C++20 modules**, selected by a CMake flag, with the
classic header build preserved for toolchains that lack module support. Aim: a *deep* adoption (true
module interfaces + partitions, not re-export shims) that also **dogfoods** our module indexer.

## Motivation

- **See modules shine.** Explicit interfaces (`import srctrl.cxx;` instead of dozens of `#include`s),
  no header-ordering fragility, no macro leakage across TUs, and faster *incremental* builds (a BMI is
  parsed once, not re-parsed per includer).
- **Dogfooding.** We just built and stress-tested (Vulkan-Hpp, 10.6k-line module) a C++20-module
  *indexer*. Building Sourcetrail itself as modules yields a large, first-party module codebase to
  index **with Sourcetrail** — the ultimate self-hosting proof and stress test. See
  [DESIGN_MODULE_PREBUILD.md](DESIGN_MODULE_PREBUILD.md).
- **No compatibility regression.** A compiler/toolchain without usable modules (older Clang/GCC, a
  generator without module scanning, moc-in-the-way GUI builds) keeps building exactly as today.

## Goals / non-goals

- **Goal:** one source tree, two builds, chosen by `-D SOURCETRAIL_CXX_MODULES=ON|OFF` (default OFF).
- **Goal:** modularize the **indexer path** first — `Sourcetrail_lib` (665 files, **moc-free**) +
  `Sourcetrail_lib_cxx` (116 files, Clang libTooling, moc-free) → the `sourcetrail_indexer` binary
  becomes a pure module consumer.
- **Non-goal (for now):** `Sourcetrail_lib_gui` (358 files, **114 `Q_OBJECT`**). moc does not reliably
  emit module-aware code yet; the GUI stays header-based. It's a later, optional phase.
- **Non-goal:** modularizing third-party deps. Qt, Clang (133 include sites), flatbuffers, nlohmann,
  glaze, toml++, stdexec, boost are consumed as headers in each module's **global module fragment**.

## The dual-build mechanism

> **Phase 0 correction.** The original sketch here toggled `module;` / `export module` with `#ifdef`
> inside a single file. Phase 0 proved that **doesn't compile**: the compiler requires `module;` and
> `export module` to be the *literal, first* tokens — they may not sit behind an `#ifdef`, a macro, or
> any preprocessor conditional (`error: 'module;' can appear only at the start of the translation
> unit`). The working mechanism, validated on this toolchain and in-repo, is a **header + a separate
> module wrapper** (the pattern {fmt} uses).

### Header + module wrapper (per module)
Each module is a **pair**:

- **`foo.h`** — the real declarations, decorated with `SRCTRL_EXPORT`, and guarding its own
  system/third-party includes with `SRCTRL_MODULE_PURVIEW` so they aren't pulled into the module
  purview:
  ```cpp
  #include "SrctrlModule.h"
  #ifndef SRCTRL_MODULE_PURVIEW      // in a module build these are hoisted to the wrapper's GMF
  #include <vector>
  #include <clang/AST/Decl.h>
  #endif
  SRCTRL_EXPORT class CxxName { /* ... */ };   // SRCTRL_EXPORT = `export` or nothing
  ```
- **`foo.cppm`** — compiled *only* in a module build (a `FILE_SET CXX_MODULES` source). The
  `module;` / `export module` lines are literal:
  ```cpp
  module;
  #include <vector>                 // GMF: everything the exported headers need
  #include <clang/AST/Decl.h>
  export module srctrl.cxx:name;    // a partition of srctrl.cxx
  #define SRCTRL_MODULE_PURVIEW
  #include "foo.h"                  // decls enter the purview; SRCTRL_EXPORT exports them
  ```

Consumers switch at their own top: `#ifdef SRCTRL_MODULE_BUILD import srctrl.cxx; #else #include
"foo.h" #endif` (an `import` *may* be `#ifdef`'d — only the module-declaration lines may not).

`SRCTRL_EXPORT` (and the `SRCTRL_MODULE_PURVIEW` convention) live in one tiny scaffolding header,
`SrctrlModule.h`, `#define`d per build mode.

The third-party include list is duplicated (header's guarded block + wrapper GMF) — a wart intrinsic
to the technique; keeping the wrapper a thin mirror of the header's includes contains it.

### CMake
- `option(SOURCETRAIL_CXX_MODULES "Build first-party C++ as C++20 modules" OFF)`.
- **Baseline is now CMake ≥ 4.4.0** (`cmake_minimum_required`), which defaults **CMP0155 = NEW** so
  C++20+ sources are scanned for `import` automatically — no per-target `CXX_SCAN_FOR_MODULES` opt-in
  (a step Phase 0 needed before the bump). Still requires the Ninja (or Visual Studio) generator and a
  module-capable compiler (Clang ≥ 16, GCC ≥ 14, MSVC ≥ 19.36); `cmake/SourcetrailCxxModules.cmake`
  probes these and **auto-falls back to the header build with a `message(WARNING …)`** — never
  hard-fails.
- ON: `.cppm` wrappers are registered via `target_sources(tgt PRIVATE FILE_SET CXX_MODULES FILES …)`,
  `SRCTRL_MODULE_BUILD` is defined, and CMake drives module ordering.
- OFF: today's `GLOB_RECURSE` header build, untouched (verified: with the flag OFF the POC target is
  absent and the generated build is unchanged).

### Macros can't be modularized
Modules don't export macros. The 23 function-like macros in `lib` — above all the pervasive `LOG_*`
family (`utility/logging`) and the messaging macros — must stay in **header companions** that are
`#include`d even in module mode (a module provides the *functions*; a sibling `logging_macros.h`
provides the macros). This seam is explicit and documented per module.

## Module decomposition (deep, with partitions)

Partitions let one logical module span many files while presenting a single `import` surface —
this is where "shine" comes from. Proposed top-level modules (bottom-up in the dependency DAG):

| Module | From | Partitions (examples) |
|---|---|---|
| `srctrl.utility` | `lib/utility` | `:file`, `:text`, `:messaging`, `:scheduling` |
| `srctrl.data` | `lib/data` | `:graph`, `:name`, `:location`, `:parser` |
| `srctrl.storage` | `lib/data/storage` | `:sqlite`, `:ladybug`, `:type` |
| `srctrl.project` / `srctrl.settings` | `lib/project`, `lib/settings` | — |
| `srctrl.cxx` | `lib_cxx` | `:parser`, `:name`, `:indexer`, `:project`, `:modules` |

A consumer then writes `import srctrl.cxx;` rather than reaching into `data/parser/cxx/...`. Intra-module
partition cycles are allowed (partitions of one module see each other); **inter-module import cycles are
forbidden** — the current header graph must be acyclic at module granularity (breaking any residual
cycles in `lib` is prerequisite work, and a good cleanup in its own right).

## Hard problems (ranked)

1. **`lib` is large (665 files) and foundational.** Modularize bottom-up by subsystem; each layer
   ships independently. This is the bulk of the effort.
2. **Clang libTooling headers in `lib_cxx`** (133 include sites, heavily templated
   `RecursiveASTVisitor`): they sit in module GMFs, making `srctrl.cxx` BMIs expensive to build. We've
   shown Vulkan-scale BMIs (62 MB) build fine, so this is a cost, not a blocker.
3. **Macro APIs** (`LOG_*`): header companions (above).
4. **flatbuffers-generated headers**: GMF-only; the generator is unaffected.
5. **Import cycles**: must be broken at module granularity.
6. **moc + modules** (`lib_gui`): deferred entirely.
7. **`import std;`**: once the core is modular, the indexer can `import std;` too — and we already build
   the std module BMI (DESIGN_MODULE_PREBUILD Phase F), so this is a natural capstone.

## Phased plan

- **Phase 0 — scaffolding, no conversions. ✅ DONE.** Added the `SOURCETRAIL_CXX_MODULES` flag, the
  `cmake/SourcetrailCxxModules.cmake` capability probe + auto-fallback, the `SrctrlModule.h` macro
  header, and the CMake dual-path plumbing (`src/cxx_modules_poc/`). Proved the hand-written
  `srctrl.ping` module builds and runs in-repo with the flag ON (`srctrl_ping() = pong`, a real
  `srctrl.ping.pcm` BMI), and that with the flag OFF the POC target is absent and the configure is
  unchanged. Bumped `cmake_minimum_required` to **4.4.0** (CMP0155 = NEW → automatic import scanning).
  Corrected the dual-build mechanism to the header + wrapper split (see above).
- **Phase 1 — pilot.** Convert one leaf to dual-build: **`AidKit_lib`** (3 files, moc-free) or a
  self-contained `lib/utility` component (`Id`, `FilePath`). Gate: builds & tests pass **both** ways,
  and Sourcetrail indexes the module build.
- **Phase 2 — `srctrl.utility`, then `srctrl.data`/`srctrl.storage`.** Bottom-up through `lib`,
  one module (with its partitions) per step, each independently shippable and testable both ways.
- **Phase 3 — `srctrl.cxx`.** Modularize `lib_cxx` with partitions; absorb the Clang-header BMI cost.
- **Phase 4 — the indexer binary.** `src/indexer/main.cpp` becomes a pure consumer:
  `import srctrl.cxx;` (+ optionally `import std;`).
- **Phase 5 — dogfood.** Index the module-built Sourcetrail *with* the module-built Sourcetrail; diff
  the symbol/edge graph against the header build's graph (they must match) and benchmark incremental
  rebuilds ON vs OFF.
- **Phase 6 — GUI (optional/later).** `lib_gui` + moc, once moc/modules matures.

## Verification

- **Dual-build CI:** every phase builds and runs the test suite with the flag **OFF and ON**; both must
  pass. OFF is the compatibility guarantee.
- **Graph-equivalence:** the module build and the header build must produce the *same* index of a fixed
  fixture project — modules change the build, not the semantics.
- **Self-index:** the module-built tree is itself a large real C++20-modules codebase; indexing it with
  the modular indexer is the capstone dogfood (and the biggest stress test yet — far past Vulkan).
- **Incremental-build benchmark:** quantify the promised speedup to justify keeping the flag ON.

## Risks

- **Toolchain maturity** — Clang/CMake module-scanning and moc bugs; mitigated by the OFF fallback.
- **BMI cost for `srctrl.cxx`** — the Clang-header GMF is heavy; incremental wins must outweigh it.
- **Dual-build maintenance** — every interface file carries the macro scaffolding; contributors must
  keep both modes compiling (CI enforces).
- **Cycle-breaking churn** in `lib` — real work, but a standalone improvement regardless of modules.
