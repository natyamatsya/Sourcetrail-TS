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
- **Phase 1 — pilot: `AidKit_lib`. ✅ DONE.** Converted all three components (`enum_class`,
  `thread_shared`, `qt/Strings`) to dual-build. Both ways green: header build **43/43** tests, module
  build **43/43** with the template tests genuinely `import aidkit;` (verified: `SRCTRL_MODULE_BUILD`
  set + `enum_class_test`'s modmap binds `aidkit=…/aidkit.pcm`). Sourcetrail indexes the module build
  0 errors, producing an `aidkit` **module node** with `enum_class`/`thread_shared` and their members
  attached to it. Refinements Phase 1 forced (roadmap corrected accordingly):
  - **`SRCTRL_EXPORT` gates on `SRCTRL_MODULE_PURVIEW`, not `SRCTRL_MODULE_BUILD`.** A library's own
    `.cpp`s #include its headers as ordinary (non-module) TUs even in a module build, and `export`
    there is ill-formed — so `export` must key off *being pulled into a wrapper's purview*, not the
    target being a module build. `SrctrlModule.h` is deliberately un-include-guarded so it re-evaluates.
  - **Header-consumer safety by making out-of-line code inline.** `qt/Strings`' 3 UDLs were out-of-line
    in `Strings.cpp`; `lib_gui` (`QtActions`, `QtLicenseWindow`) uses them via `#include`. A
    module-attached out-of-line symbol would leave those header consumers unresolved, so the impls
    moved to `Strings.inl` as `inline` (vague linkage → each consumer self-contained). A module may
    only replace a leaf consumed by non-module code if everything it exports is a template or inline.
  - **`.inl` convention** for inline impls kept out of headers (`Strings.inl`).
  - **`FILE_SET CXX_MODULES` must be `PUBLIC`**, not `PRIVATE`, for another target to `import` it.
  - **Scaffolding graduated** to `src/scaffolding/SrctrlModule.h` on a global include path.
- **`SOURCETRAIL_CXX_IMPORT_STD` option — WORKS (gap closed).** A compile-time toggle so module
  wrappers `import std;` instead of #including std headers (`SRCTRL_IMPORT_STD` → the wrapper switches;
  targets set `CXX_MODULE_STD ON`). The full recipe for brew LLVM 22 / macOS:
  1. **Experimental gate** `CMAKE_EXPERIMENTAL_CXX_IMPORT_STD = f35a9ac6-8463-4d38-8eec-5d6008153e7d`
     (CMake-4.4-specific) + `CMAKE_CXX_STANDARD 23`, both set **before `project()`** (the top-level
     CMakeLists reads the cached option value to do this).
  2. **`libc++.modules.json`** isn't found by `-print-file-name` (brew ships it in `lib/c++/`), so the
     toolchain file sets `CMAKE_CXX_STDLIB_MODULES_JSON = ${LLVM_PREFIX}/lib/c++/libc++.modules.json`.
  3. **A mach-o archiver** — `CMAKE_AR = /usr/bin/ar` (already in the toolchain; GNU `ar`/`llvm-ar`
     produce archives macOS `ld` rejects, which is what the earlier "std-module archive won't link"
     was — an *isolated-test* artifact, not a real blocker).

  Verified end-to-end: `srctrl.utility` compiled with `import std;` (its wrapper drops the std
  #includes) links and runs, **including interop** — a consumer that `#include`s std headers uses the
  module's exported helpers fine. Note: a Qt-touching module (AidKit) keeps `<QString>` in the GMF, so
  `import std` there mixes with Qt's std includes; the option is really for std-only modules like
  `srctrl.utility`.
- **Phase 2 — `srctrl.utility`: first `lib` module. ✅ STARTED (utilityEnum → `:enums` partition).**
  Converted `utilityEnum.h` (header-only concepts/templates) into `srctrl.utility` with an `:enums`
  partition (`srctrl_utility.cppm` primary `export import :enums;` + `srctrl_utility-enum.cppm`). Both
  ways verified: `utilityEnum.h` still works as a plain header (OFF), and `import srctrl.utility;` +
  the user-specialized `intToEnum` primary template all work (ON, isolated). **`Sourcetrail_lib`
  builds in ON mode** — the module BMIs build and the other ~664 header-consumer sources compile
  alongside it (coexistence). `UtilityEnumTestSuite` converted to the import toggle and compiles
  against the module; AidKit_test (43/43) and the POC still pass. Sourcetrail indexes the module
  (2/2, 0 errors, `srctrl.utility` + `:enums` BMIs). Three findings, each a roadmap correction:
  - **Partition names can't be keywords.** `export module srctrl.utility:enum;` fails to parse
    (`enum` is a keyword) -- had to name the partition `:enums`.
  - **`clang-scan-deps` chokes on stdexec** (`token is not a valid binary operator in a preprocessor
    subexpression`), and CMP0155 otherwise scans *every* C++23 source in a module-linked target
    (lib/lib_cxx/lib_gui all include stdexec). Fix: **default `CMAKE_CXX_SCAN_FOR_MODULES OFF`
    project-wide** and re-enable it per-file (`CXX_SCAN_FOR_MODULES ON` source property) only on the
    handful of files that actually `import`. `FILE_SET CXX_MODULES` interface units are scanned/built
    regardless. This is the standing model for the migration.
  - **`import std` is not the blocker some thought**: the earlier "archive link fails" was the
    isolated test's default `llvm-ar`; the real toolchain uses `/usr/bin/ar` (mach-o) and links module
    archives fine (AidKit proved it).
- **Phase 2 (cont.) — `srctrl.utility` grown to `:cache` + `:types` partitions.** Folded in seven more
  header-only leaves: `SingleValueCache`/`OrderedCache`/`UnorderedCache` (→ `:cache`) and
  `Status`/`Tree`/`Property`/`ScopedSwitcher` (→ `:types`). The primary `export import`s all three
  partitions; a consumer `import srctrl.utility;` gets them all. Verified: all three partitions import
  together (isolated), the 7 headers still compile as plain headers (OFF), `Sourcetrail_lib` builds ON
  with the 4 module files, and the module indexes 0 errors. **Key finding — owned header-defined types
  are safe across the import/#include boundary**: a `Box<int>` created via `#include` and one obtained
  via `import` are the *same type* (tested). So the boundary rule is narrower than feared — only
  *out-of-line symbols* (functions/vars in a `.cpp`) mismatch across the boundary; header-only types
  (class templates, inline structs) don't, which is why these leaves fold in cleanly while consumers
  still `#include` them. Out-of-line utility (utilityString, FilePath) still needs the inline/`.inl`
  treatment (or consumer conversion) before it can join a module.
- **Phase 2 (cont.) — `utilityString` inlined and folded in as `:string`.** The first *out-of-line*
  utility joins the module: `utilityString.cpp`'s pure-std functions moved to `utilityString.inl` as
  `inline` (so header consumers get their own definitions — vague linkage — and don't depend on
  module-attached symbols), declared `SRCTRL_EXPORT` in the header, and exported via a `:string`
  partition. The 4 **Qt-dependent** locale functions (`toLowerCase`, `convertToUtf32`,
  `isCaseInsensitive{Equal,Less}`) have pure-std *signatures* but `QString` *bodies*, so they stay
  out-of-line in `utilityString.cpp` and are **not** exported — an include-only seam (like the `LOG_*`
  macros), so the ~hundreds of non-Qt `utilityString` consumers don't pay for `<QString>`. Verified:
  `Sourcetrail_lib` builds **OFF** (all 52 `utilityString` consumers recompile with the inline
  functions) and **ON** (5 module BMIs), and `:string` imports (`utility::split`/`trim`/`elide`).
  - **Latent OFF-build bug fixed here:** CMP0155 (NEW under the 4.4 baseline) scans C++20+ sources for
    `import` *regardless of `SOURCETRAIL_CXX_MODULES`*, so the stdexec `clang-scan-deps` failure broke
    even the plain header build once a stdexec-including target was compiled. `set(CMAKE_CXX_SCAN_FOR_MODULES
    OFF)` is now **unconditional** (was inside the modules-enabled block), not just for module builds.
- **Next — `srctrl.data`/`srctrl.storage`.** Bottom-up through `lib`, adding partitions per step.
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
