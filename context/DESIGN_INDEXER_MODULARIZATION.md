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
- **`SOURCETRAIL_CXX_IMPORT_STD` option — GREEN for the whole first-party module set (see the
  make-it-green finding below).** A compile-time toggle so module wrappers `import std;` instead of
  #including std headers (`SRCTRL_IMPORT_STD` → the wrapper switches; targets set `CXX_MODULE_STD ON`).
  The full recipe for brew LLVM 22 / macOS:
  1. **Experimental gate** `CMAKE_EXPERIMENTAL_CXX_IMPORT_STD = f35a9ac6-8463-4d38-8eec-5d6008153e7d`
     (CMake-4.4-specific) + `CMAKE_CXX_STANDARD 23`, both set **before `project()`** (the top-level
     CMakeLists reads the cached option value to do this).
  2. **`libc++.modules.json`** isn't found by `-print-file-name` (brew ships it in `lib/c++/`), so the
     toolchain file sets `CMAKE_CXX_STDLIB_MODULES_JSON = ${LLVM_PREFIX}/lib/c++/libc++.modules.json`.
  3. **A mach-o archiver** — `CMAKE_AR = /usr/bin/ar` (already in the toolchain; GNU `ar`/`llvm-ar`
     produce archives macOS `ld` rejects, which is what the earlier "std-module archive won't link"
     was — an *isolated-test* artifact, not a real blocker).

  Verified for a **std-only** partition: `srctrl.utility:enums` compiled with `import std;` (its wrapper
  drops the std #includes) links and runs, **including interop** — a consumer that `#include`s std headers
  uses the module's exported helpers fine. Note: a Qt-touching module (AidKit) keeps `<QString>` in the
  GMF, so `import std` there mixes with Qt's std includes; the option is really for std-only modules —
  **unless Qt itself is behind an import (see `srctrl.qt` below).**

  **RE-VERIFIED ON THE FULL MODULE GRAPH (2026-07-21, Phase-5-complete tree):** the entire ON build —
  all srctrl modules incl. the clang-heavy srctrl.cxx, srctrl.interprocess, `import thoth.ipc;`, the
  importer indexer main.cpp — builds, tests (2656 assertions), and indexes (Usages 529/2041/0 +
  partition/collision fixture) with `import std` on. Three findings:
  1. **TOOLCHAIN BUG (brew LLVM 22.1.8 + Apple SDK): stock std.cppm does not compile** — "use of
     undeclared identifier 'INFINITY'/'NAN'" from `<complex>`. The SDK math.h delegates INFINITY/NAN
     to clang's float.h via the `__need_infinity_nan` re-include protocol whenever
     `__has_feature(modules)` (true in ALL c++23 clang-22 compiles, not just module units), but
     libc++'s float.h *wrapper* has a plain `_LIBCPP_FLOAT_H` guard that swallows the re-include once
     `<cfloat>` was seen — and std.cppm's GMF includes `<cfloat>` (alphabetical) before
     `<cmath>`/`<complex>`. Reproducible in a plain TU: `#include <cfloat>` + `#include <complex>`.
     WORKAROUND in the mac toolchain file: patched build-dir copies of std(.compat).cppm that
     `#include <math.h>` first (runs the float.h dance before the guard latches); a patched
     modules.json points source-path at the copies while system-include-directories keeps the
     original share dir for the .inc bodies. `CMAKE_CXX_STDLIB_MODULES_JSON` is FORCE-repointed.
  2. **CMake caches import-std state at compiler DETECTION time** (`CMakeCXXCompiler.cmake`:
     `CMAKE_CXX_COMPILER_IMPORT_STD`, and the modules-json path). Enabling the option on an existing
     build dir — or changing the json — requires `rm -rf <build>/CMakeFiles/<cmake-ver>/` to force
     redetection; a plain reconfigure silently keeps the old state (observed both ways: "not enabled
     when detecting toolchain", and a patched json being ignored).
  3. Two first-party fixes of the documented classes: a bare-`size_t` sweep miss in `utility.h`
     (8 sites → `std::size_t`), and POSIX `localtime_r` in TimeStamp.inl — `<time.h>` now included
     unconditionally in the `:time` wrapper GMF (**rule: POSIX/C-platform headers are NOT covered by
     `import std` and stay textual in the import-std build**).

- **`srctrl.qt` — a Qt import-module wrapper dissolves the "Qt blocks import std" barrier (capability
  proven; one integration hits a clang crash).** Qt 6.11 ships no C++20 modules, so we wrap it like
  sqlpp23: `srctrl.qt` with partitions mirroring Qt's own names (`:core` = QtCore value types
  `QString`/`QByteArray`/`QRegularExpression`/env fns; `:meta` = the `QMetaType` system; future `:gui`/
  `:widgets`), re-exported via `export using ::QString;`. **The key result: `import srctrl.qt` + `import
  std` coexist** — the consumer builds *and runs*, doing `QString`↔`std::string` interop with `std::string`
  from `import std`. Putting Qt behind an `import` confines its textual std-pull to the wrapper's own TU, so
  the "Qt drags std into the GMF" conflict that blocks import std **disappears**. This unblocks modularizing
  the Qt-coupled foundational types into the import-std-clean world.
  - **Caveat — Qt macros.** `QStringLiteral`, `Q_DECLARE_METATYPE`, `Q_OBJECT` are macros and can't cross
    an `import`; a consumer keeps that one macro textual (replace `QStringLiteral("x")` with
    `QString::fromUtf8("x")`, or keep a minimal `<QMetaType>` for `Q_DECLARE_METATYPE`).
  - **Pilots.** `TimeStamp` (pure std, no Qt) modularized cleanly as `srctrl.utility:time`. `FilePath` —
    the real Qt pilot — is **reverted for now**, because of a clang crash whose trigger is now pinned down:
    - **The crash is `re-export`-specific.** Routing a Qt-coupled partition through a module that
      **`export import`s** it — `srctrl.utility` re-exporting `:file` (which imports srctrl.qt) — segfaults
      clang's frontend when a consumer `import srctrl.utility` + uses the type. The direct pattern is fine
      (static `QRegularExpression` + `qgetenv` in an inline member imported straight from srctrl.qt compiles).
    - **The dodge — import the Qt-coupled type as a leaf module, directly.** Verified: a leaf module that
      `import`s srctrl.utility (all 7 partitions) + srctrl.qt + srctrl.logging and has an inline member using
      `QRegularExpression` + `utilityString` + `srctrl::log` + `qgetenv`, imported **directly** by the
      consumer, compiles fully (frontend green; only app-link symbols missing). So a Qt-coupled type wants
      its own leaf module (e.g. `srctrl.file`), NOT a partition re-exported by srctrl.utility.
    - **Full migration attempted — BLOCKED, and the real trigger is worse than re-export.** Built the whole
      thing (FilePath → standalone `srctrl.file`; switched its 4 GMF-consumer wrappers — data:location/graph,
      storage:types/access — to `import srctrl.file`; dropped FilePath's use of the unexported Qt-seam
      `utility::toLowerCase` in favor of an inline `QString(...).toLower()`). All 19 module units compile,
      **and re-export is fine** — a consumer that `import srctrl.data` (which re-exports `:location`, which
      imports `srctrl.file`) and uses `SourceLocationFile` compiles cleanly. But **importing `srctrl.file`
      directly and instantiating `FilePath` at all — even the bare `FilePath p("/x");` constructor —
      segfaults clang's frontend.** So it's not the re-export and not any specific method; it's deserializing/
      instantiating FilePath's class from a module BMI. The synthetic mimic didn't crash because it never
      instantiated a *real* Qt-coupled class of FilePath's shape. Since FilePath is instantiated directly by
      countless (future module) consumers, this makes it unusable as a module. **Reverted; FilePath stays
      classic.** `srctrl.qt` (the wrapper + the import-std coexistence) and `TimeStamp` (`:time`) stand.
    - **The crash is NOT Qt — confirmed by refactoring it away.** Made FilePath fully **Qt-free**
      (`refactor(FilePath)` commit `ff778ee0`: elementary `${VAR}`/`%VAR%` scanning instead of
      `QRegularExpression`+`qgetenv`; a plain `std::tolower` loop instead of the Qt-seam
      `utility::toLowerCase` — a good cleanup regardless). The pure-std FilePath, as a leaf module, **still
      segfaults** on `FilePath p("/x");`. So it was never Qt. Bisection then ruled out the member type
      (minimal modules with `unique_ptr<filesystem::path>` / value `filesystem::path` / `unique_ptr<string>`
      members all compile), the baseline (a stripped FilePath — ctors + `str`/`extension`/`empty` — compiles),
      and `expandEnvironmentVariables` (stripped FilePath + that method + the real srctrl.utility/logging
      imports compiles). The earlier guess that this was an "aggregate BMI-deserialization bug" was **wrong**
      — the actual root cause is now pinned exactly, and **FilePath IS modularizable** with a one-line build flag.
    - **ROOT CAUSE — [LLVM #166068](https://github.com/llvm/llvm-project/issues/166068): reduced-BMI drops the
      global `operator new`/`delete`.** The crash backtrace is not in the AST reader at all — it's in **CodeGen**:
      `CodeGenFunction::EmitBuiltinNewDeleteCall` (`CGExprCXX.cpp:1384`), reached via `EmitBuiltinExpr` →
      `EmitCallExpr`, under `CodeGenModule::EmitDeferred()`. The clang note pinpoints it:
      `__new/allocate.h:35: Generating code for declaration 'std::__libcpp_allocate'`. In **reduced-BMI mode**
      (clang's default since 20) the replaceable global `operator new`/`operator delete` declarations are pruned
      from the BMI. When a consumer instantiates FilePath, its inline members allocate via
      `std::filesystem::path` → `std::__libcpp_allocate` → `__builtin_operator_new`, whose CodeGen looks up the
      *predeclared* global operator — which is missing from the BMI → in a release (NDEBUG) clang the assertion
      "predeclared global operator new/delete is missing" degrades to a **null-deref/segfault**. This is why
      dropping the `unique_ptr` never helped (`std::filesystem::path` allocates regardless) and why
      self-contained non-module reconstructions never crashed (no reduced BMI in the path). Upstream fix:
      PR #167468 (adds allocation functions to the reduced BMI unconditionally).
    - **FIX: `-fno-modules-reduced-bmi`.** With it, `import srctrl.file; FilePath p("/x");` **compiles, links,
      and runs** (verified in isolation against the real `srctrl.file`/`srctrl.utility`/`srctrl.logging` module
      sources). Applied in `src/lib/CMakeLists.txt` as a `PRIVATE` compile option on `Sourcetrail_lib` under
      `SOURCETRAIL_CXX_MODULES_ENABLED` (Clang-guarded) — the flag only has to be on the BMI *producer*, so
      consumers read the retained operators and never need it. Note this is a **general** fix for the whole
      modules migration, not FilePath-specific: any module whose inline members allocate would hit the same bug
      once consumers `import` it.
    - **No source-level workaround.** Tried three ways to force the operators back into the reduced BMI from the
      module source — an exported inline that ODR-uses `::operator new`/`delete`; explicit redeclaration in the
      purview (ill-formed: replaceable globals can't attach to a named module); an exported inline using
      `__builtin_operator_new`/`delete` directly. All three still crash: the pruning is in clang's ASTWriter and
      isn't reachable from user code. So the compiler flag is the only lever until PR #167468 ships.
    - **FilePath is now converted to `srctrl.file`** (dual-build `.h`/`.inl` + `srctrl_file.cppm`, wired into
      the `FILE_SET CXX_MODULES`). The **classic build is unaffected** (inline members in `FilePath.inl`, `.cpp`
      is a one-line anchor) — verified by building `Sourcetrail_lib` + a headless full index (2/2 files, 0
      errors, 435 files, all paths well-formed).
    - **Remaining blocker for the full-app modules-ON build (pre-existing, unrelated to FilePath).** Configuring
      the whole project with `SOURCETRAIL_CXX_MODULES=ON` fails at CMake generate with an inter-target
      dependency cycle (`Sourcetrail_lib` ↔ `Sourcetrail_lib_gui` ↔ `Sourcetrail_lib_cxx` ↔ `Sourcetrail_res_gui`
      — these static libs mutually depend, and CMake's `@synth_0` module-target synthesis can't tolerate the
      cycle). This is orthogonal to FilePath (it affects all 21 module units equally) and is the next thing to
      untangle before an end-to-end modules-ON app build; module units remain validated in isolation meanwhile.
    - **Follow-up modernization (independent of modules).** With FilePath staying a classic header, it got a
      cleanup pass: the `std::unique_ptr<std::filesystem::path>` member became a **plain `std::filesystem::path`
      value** (kills a heap allocation on a value type instantiated constantly; the header already includes
      `<filesystem>` and nothing treated it as nullable). The two platform statics became header-inline
      **`constexpr`**: `getEnvironmentVariablePathSeparator()` (`char`) is now a **private** helper (its only
      use is internal, splitting `expandEnvironmentVariables()` output); `getExecutableExtension()` was a
      *platform* concern, not a path one, so it **moved to `utility::Platform::getExecutableExtension()`**
      (`constexpr std::string_view`, sitting next to `isWindows`/`getName`) — leaving FilePath's public surface
      with **no** platform statics at all. `FilePath.cpp` no longer includes `Platform.h`. The 6 call sites
      moved to `utility::Platform::getExecutableExtension()` via `std::format` (`const char[] + string_view`
      doesn't compose). Verified by a headless full index of `testing/usages`: 2/2 files, 0 errors, 435 indexed
      files with all paths well-formed (0 empty / double-slash / unresolved).

- **`import std` — now GREEN for the whole first-party module set (utility + data + logging).** Getting
  there took fixing two real import-std-with-legacy-headers edges (both pre-existing, surfaced only when
  the full set was actually built with the option on) — and, importantly, fixing them the *right* way
  rather than papering over them by pulling C compatibility headers into the GMF:
  1. **Bare `::size_t` → `std::size_t`.** First-party headers used unqualified `size_t`; `import std`
     exports `std::size_t` but not the global `::size_t`, so once the C++ std #includes drop out the name
     is undeclared. The clean fix is to qualify it — a 119-site `size_t`→`std::size_t` sweep across the
     converted headers/`.inl`s (caches, utilityString, SourceLocation*, NameHierarchy, Node/Edge/Graph).
     (The C-header workaround `#include <stddef.h>` was rejected; note `<cstddef>` is doubly wrong here —
     it defines `std::byte`, dragging `<type_traits>` into the GMF and *causing* edge #2.)
  2. **`.inl` std includes must be guarded too.** `utilityString.inl` had unguarded `#include <algorithm>`
     / `<cctype>`; included in the purview, they re-declared libc++ internals that `import std` already
     provided (`declaration of 'integral_constant' … follows declaration in the global module`). Fix:
     guard them with `#ifndef SRCTRL_MODULE_PURVIEW` exactly like the headers — the wrapper GMF (or
     `import std`) supplies them in a module build. **Rule: a converted `.inl`'s std includes get the same
     `SRCTRL_MODULE_PURVIEW` guard as the header's.**
  Verified: all 11 first-party module units compile with `SOURCETRAIL_CXX_IMPORT_STD` on (std module BMI
  built; only app-level `FilePath`/Qt symbols unlinked in the isolated harness — identical to the
  non-import-std harness). The `SRCTRL_IMPORT_STD` guard is applied uniformly across every wrapper, and
  both the non-import-std module build (11/11) and the classic modules-OFF build (`Sourcetrail_lib`
  green) are unaffected by the `std::size_t` qualification.
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
- **Phase 2 (cont.) — `srctrl.data` started (`:types`).** The **second** first-party module: a
  `:types` partition with `TooltipOrigin` (header-only enum) and `NameDelimiterType` (its 3 pure-std
  functions inlined to `NameDelimiterType.inl`). Verified both ways (header + `import srctrl.data;`),
  and **`Sourcetrail_lib` builds ON with two modules coexisting** (`srctrl.utility` + `srctrl.data`,
  7 BMIs total). `lib/data` is mostly out-of-line and interconnected (214 files), so most components
  need the inline/`.inl` (or split) treatment before joining.
- **Phase 2 (cont.) — `LocationType` + the first cross-module import. ✅** `LocationType` joins
  `srctrl.data:types`, and the partition now `import srctrl.utility;` — the migration's **first
  inter-module dependency**. Mechanics proven:
  - The header toggles `#include "utilityEnum.h"` (header build) vs the wrapper's `import
    srctrl.utility;` (module build) via `SRCTRL_MODULE_PURVIEW`, so utilityEnum isn't re-included/
    re-exported by `srctrl.data`.
  - `intToEnum<LocationType>` is an **explicit specialization of a template owned by another module**;
    it's inlined into `LocationType.inl` and *not* separately `export`ed (specializations are reached
    via the primary). `operator<<(size_t, LocationType)` is inlined + exported.
  - CMake orders `srctrl.utility` before `srctrl.data` from the scan of the `.cppm` imports — no manual
    dependency. Verified: `Sourcetrail_lib` builds OFF (LocationType consumers use the inline
    specialization) and ON, and a consumer importing both modules resolves `intToEnum<LocationType>`
    and `operator<<` across the boundary.
- **Phase 2 (cont.) — `name/` cluster started: `NameElement` → `srctrl.data:name`.** The first
  *concrete class* with out-of-line members joins a module. All of its members (and its nested
  `Signature` class) are inlined into `NameElement.inl`; `srctrl.data:name` `import srctrl.utility;`
  for the one `utility::substrBeforeLast` call. Verified OFF+ON (import == header build).
  - **Critical rule this proved: every member of an exported class must be inline.** An out-of-line
    member defined in an ordinary `.cpp` (global module) does NOT resolve for module *importers*
    (undefined symbol — the exported class is module-attached, so the member is mangled differently
    than the global-module definition). Header consumers still work (both global-module), but the
    module can't leave any member out-of-line.
- **Phase 2 (cont.) — `NameHierarchy` folded into `srctrl.data:name`. ✅** All members inlined into
  `NameHierarchy.inl` **except `deserialize`**, which stays out-of-line in `NameHierarchy.cpp`. Verified
  OFF+ON (importers use `serialize`/`push`/`getQualifiedName`; the real build's `-D_LIBCPP_NO_ABI_TAG`
  is required — an isolated test without it hits a Homebrew-libc++ `abi_tag` redeclaration error).
  - **New finding — a member that calls a non-modularized header which forward-declares the module's
    own type can't be inlined.** `deserialize` uses `utilityMainFunction` (`isUniquifiedMainFunction`
    etc.), whose header contains `class NameHierarchy;`. Putting it in the wrapper GMF makes
    `NameHierarchy` "declared in the global module," which then conflicts with the module's own
    `NameHierarchy` (*"declaration in module … follows declaration in the global module"*). And those
    helpers are out-of-line free functions, so they can't move to the purview either. So `deserialize`
    stays out-of-line (with `logging.h` + `utilityMainFunction.h` in its `.cpp`) — an **include-only
    member**: reachable via `#include`, not via `import`. Fine for now (no importer deserializes); it
    resolves fully once `utilityMainFunction` is itself modularized.
- **Phase 2 (cont.) — `location/` cluster folded into `srctrl.data:location`. ✅** `SourceLocation` +
  `SourceLocationFile` + `SourceLocationCollection` (~684 out-of-line lines) fully inlined into per-class
  `.inl`s; the wrapper imports `:types` (LocationType) and keeps `FilePath` / `Id` (types.h) / `logging.h`
  in the GMF (global-module, no forward-decl of a location type, so no conflict). Two new patterns this
  cluster forced (both now part of the recipe):
  - **Mutually dependent classes** (`SourceLocation` ⇄ `SourceLocationFile`). Their `.inl`s each need the
    partner complete, but in the purview the header's cycle-breaking cross-include is skipped. Fix: guard
    each header's own `#include "X.inl"` too, and have the wrapper include **all class definitions first,
    then all `.inl`s** — so every type is complete before any inline body is parsed.
  - **Forward declarations across the module boundary.** A forward decl of a *non-module* type
    (`class FilePath;`) must be guarded (skip in purview — it comes from the GMF, else "declared in module
    follows global module"). A forward decl of a *module* type (`class SourceLocationFile;`, needed for the
    cycle) must carry `SRCTRL_EXPORT`, else the plain forward decl gives it module linkage and the later
    `export class` def fails ("cannot export redeclaration … previous has module linkage").
- **Phase 2 (cont.) — `graph/` started: the `token_component/` leaf layer → `srctrl.data:graph`, plus
  three data enums folded into `:types`. ✅** The `TokenComponent` polymorphic base + its ~9 concrete
  subtypes (`TokenComponentAbstraction`/`Access`/`BundledEdges`/`Const`/`FilePath`/`Static`/
  `InheritanceChain`/`IsAmbiguous`) are now the `:graph` partition (wrapper GMF: `types.h`/`FilePath.h` +
  std; `import :types;` for `AccessKind`). The three `intToEnum`-specializing enums the graph core needs
  (`ElementComponentKind`, `DefinitionKind`, `AccessKind` — same cross-module pattern as `LocationType`)
  went into `:types` rather than the GMF, so `:graph`/Node/Edge import them instead of duplicating. New
  finding this cluster proved:
  - **Polymorphic class hierarchies cross the module boundary cleanly.** An exported base with virtual
    functions + derived overrides works for importers *and* header-consumers — virtual dispatch **and**
    RTTI (`dynamic_cast`) both resolve — provided the key function (the virtual destructor) is `inline`
    like every other member (the same all-members-inline rule; verified with an isolated test + the
    `TokenComponentStatic::copy()` interop in the data consumer).
- **Phase 2 (cont.) — `srctrl.logging`: module-native logging (compat-shim only). ✅** A standalone
  module (`LogFacade.h`/`.inl` + `srctrl_logging.cppm`) exporting `srctrl::log::{info,warning,error}`
  (plain `string_view`, brace-safe), `*f` (std::format), and `*_lazy` (callable) front ends over the
  **unchanged** classic `LogManager` singleton (kept global-module in the GMF). Replaces the LOG_* macros'
  preprocessor capture (`__FILE__`/`__FUNCTION__`/`__LINE__`) with `std::source_location::current()` as a
  defaulted argument, so nothing needs a macro to cross an `import`. Key pieces, all verified on our
  clang22/libc++/C++23 toolchain:
  - **`fmt_with_loc` consteval wrapper.** Non-terminal variadic + trailing default isn't allowed, so the
    format string + `source_location` fold into one leading argument built by an implicit `consteval`
    conversion; the trailing pack deduces normally because the wrapper's pack is pinned out of deduction
    with `std::type_identity_t`. The `*f` suffix avoids ambiguity between the `string_view` overload and
    the wrapper's converting ctor for a bare literal.
  - **Compile-out + lazy eval** — the two things macros gave that functions can't for free — are
    recovered by an exported `constexpr level min_level` gate via `if constexpr` (calls below the floor,
    args included, are discarded) and the `*_lazy(callable)` overloads (message factory runs only when
    the level is live). We had *no* per-level compile-out before (only a runtime `getLoggingEnabled()`),
    so this is a net gain.
  - **Compat-shim only:** `logging.h`'s 12 LOG_* macros are left byte-for-byte unchanged (same
    `LogManager` backend, independent front end → zero behavior change, no ODR interaction) so all ~489
    existing call sites compile untouched. Module purviews (the graph core next) will `import
    srctrl.logging` and call `srctrl::log::error(...)` directly instead of GMF-including `logging.h`. The
    102 `_STREAM` sites are deliberately **not** swept — future opportunistic migration.
  - Verified: `Sourcetrail_lib` green modules-OFF (`LogFacade.h` valid as a plain header, shim untouched);
    the module compiles ON with a consumer importing it and type-checking all four call styles (only the
    app-provided LogManager backend is unlinked in the isolated harness, as designed; the runtime path
    -- source_location capture + lazy gating -- was proven separately end-to-end with a stub backend).
- **Phase 2 (cont.) — the graph *core* folded into `:graph`; `srctrl.data` now covers all of `graph/`. ✅**
  `Token`/`Edge`/`Node`/`Graph` + the value type `NodeType` (~1100 out-of-line lines) inlined into the
  `:graph` partition, and their two classification enums `NodeKind` (intToEnum-specializing) + `NodeModifier`
  folded into `:types`. This is the **first real `srctrl.logging` consumer**: the core's `LOG_ERROR`/
  `LOG_WARNING` calls became `srctrl::log::error`/`warning`, so the wrapper `import srctrl.logging;`
  rather than GMF-including `logging.h` — the macro-in-GMF reliance is gone here. Cross-module: `import
  srctrl.utility` (utilityEnum for `intToEnum<Edge::EdgeType>`, utilityString, `Tree` for NodeType),
  `import :types` (the 5 enums), `import :name` (NameHierarchy); GMF keeps `types.h`/`FilePath.h`/
  `QtResources.h`. Findings:
  - **The `Edge`⇄`Node` cycle is asymmetric** (unlike SourceLocation⇄File). `Node`'s *class body* needs
    `Edge` complete (`Edge::TypeMask` in three signatures), while `Edge` needs only a forward decl of
    `Node`. So in the classic build `Node.h` `#include`s `Edge.h` at the top and is the single site that
    pulls **both** `.inl`s (after both classes are complete); `Edge.h` forward-declares `Node`
    (`SRCTRL_EXPORT class Node;`) and, in the classic path only, `#include`s `Node.h` after its own class
    so either entry point yields the same TU order. In the module wrapper this is invisible: it includes
    `Edge.h` then `Node.h` (both class defs), then all `.inl`s.
  - **Enums the core needs live in `:types`, not the GMF.** `NodeType` pulls `NodeKind` (intToEnum) and
    `Tree` (already `srctrl.utility`); GMF-including either would double them against the imports. Folding
    `NodeKind`/`NodeModifier` into `:types` keeps a single definition the whole module graph shares.
  - Known follow-up: the `NodeKindMask`/`NodeModifierMask` `int` typedefs are left un-`SRCTRL_EXPORT`ed
    (no importer names them yet — callers pass `int`/enum literals); export them when a consumer needs
    the spelling.
  - Verified: `Sourcetrail_lib` green modules-OFF (whole app recompiles against the converted core +
    the `srctrl::log` rewrite); ON, all 11 first-party module units build and a consumer imports
    `srctrl.data` and uses `Graph`/`NodeType(NODE_CLASS)`/`intToEnum<Edge::EdgeType>` across the boundary
    (only FilePath.cpp + the Qt tag unlinked in the isolated harness, as designed).
- **Next — `srctrl.storage`** (45 headers). **Scoped:** see `DESIGN_STORAGE_MODULARIZATION.md`. TL;DR:
  the 15 header-only `type/` POD structs are a clean, immediate `srctrl.storage:types` win (deps entirely
  on the done `:types` enums + GMF `Id`); everything past that is gated on a **sqlpp23-module spike**
  (sqlpp23 v0.69 *does* ship `import sqlpp23.core/sqlite3`, but it's experimental, self-compiled, and the
  ddl2cpp `SQLPP_CREATE_NAME_TAG` macro doesn't cross `import`) and must move in lockstep with the
  CppSQLite3→sqlpp23 SQL-layer migration (`DESIGN_STORAGE_CODEGEN.md`) rather than race ahead of it.
- **Phase 2 (cont.) — modules-ON builds the WHOLE APP in situ. ✅** First full-tree build with
  `SOURCETRAIL_CXX_MODULES=ON` in the primary build dir (not the isolated harness): app + indexer +
  test all link with the 24 first-party module units compiled; test suite (728) and the headless
  Usages index (529 nodes / 435 files) are identical ON vs OFF. Getting there surfaced a class of
  **cross-BMI attachment clashes** the harness never could: a textual (global-module) copy of a header
  in ANY module unit's GMF clashes with the same entity attached to a named module once both BMIs meet
  in one importer — even transitively (the global copy rides along inside the carrying module's BMI).
  Rule distilled: **once a header is modularized, no module unit's GMF may textually include it,
  directly or through any other GMF header.** Fixes this forced:
  - **FilePath restored to a leaf**: the Qt-drop refactor had added `utilityString.h` to `FilePath.inl`
    (for `utility::splitToVector`); inlined an elementary split instead, so GMF-including `FilePath.h`
    no longer drags `:string`-attached declarations. `srctrl.file` dropped its `import srctrl.utility`.
  - **`srctrl.data:bookmark`** (new partition): Bookmark/BookmarkCategory/EdgeBookmark/NodeBookmark
    converted (.cpp bodies inlined into .inl); their textual `TimeStamp.h` in the storage GMFs was the
    global copy clashing with `srctrl.utility:time`.
  - **`srctrl.file` grew `FileInfo` + `TextAccess`** (both need FilePath; FileInfo needs TimeStamp, so
    the module now `import srctrl.utility`) — and every wrapper that GMF-included `FilePath.h`
    (`:location`, `:graph`, `storage:types`, `storage:access`) migrated to `import srctrl.file`, the
    module's first consumers.
  - **`tracing.h` gated behind `TRACING_ENABLED`**: with tracing off it reduces to the empty `TRACE()`/
    `PRINT_TRACES()` macros and includes nothing, making it GMF-safe (it used to textually pull
    FilePath + TimeStamp into `storage:interface`).
- **Phase 2 (cont.) — mid-layer unblock 1: `srctrl.file` grows the full file/path cluster. ✅**
  `FileSystem`, `FileTree`, `AppPath`, `UserPaths`, `ResourcePaths` converted (.cpps inlined; the
  AppPath/UserPaths static registries become C++17 inline variables in the .inls). Two findings:
  (a) `AppPath.cpp`'s `#include "utilityApp.h"` was vestigial — it only used
  `Platform::getExecutableExtension()`, already a GMF citizen; (b) `FileSystem` called the Qt/locale
  `utility::toLowerCase`, which is *deliberately excluded* from `srctrl.utility:string` — replaced
  with an elementary ASCII `toLowerCaseAscii` detail helper (file extensions are ASCII; mirrors
  `FilePath::getLowerCase()`'s Qt-drop precedent). This unblocks the lib_cxx deferrals that hung on
  ResourcePaths/FileTree; the remaining mid-layer blockers are messaging (MessageStatus), settings
  (ApplicationSettings/ToolChain — gated on splitting FilePath-dependent helpers out of `utility.h`,
  which would otherwise cycle srctrl.utility ↔ srctrl.file), TextCodec (Qt), and utilityApp (QProcess).
- **Phase 2 (cont.) — mid-layer unblock 2: `utility.h` split → `srctrl.utility:containers`. ✅**
  `utility.h`'s only FilePath coupling was the `toStrings<FilePath>` specialization — moved to a new
  `utilityFilePath.h` (classic-build seam, 3 call sites), the out-of-line `digits()` inlined,
  `utility.cpp` deleted. The now FilePath-free header becomes the `:containers` partition
  (`SRCTRL_EXPORT namespace utility` — first use of an exported namespace-definition instead of
  per-declaration tags). This kills the srctrl.utility ↔ srctrl.file cycle risk for good and makes
  `utility.h` safe in any module GMF — unblocking ToolChain (its impl deps are now utility.h +
  macro-only headers). Remaining blockers: messaging, settings, TextCodec/utilityApp (Qt-facing).
- **Phase 2 (cont.) — `srctrl.settings` (mid-layer unblock 4, the last poison header). ✅**
  ConfigManager (TOML/JSON key-value store; glaze + toml++ stay GMF third-party) + Settings +
  ApplicationSettings converted into a new `srctrl.settings` module (imports srctrl.utility /
  srctrl.file / srctrl.logging); `utilityFile` joined `srctrl.file` on the way (its header
  fwd-declared FilePath — finding (a)). ApplicationSettings' fwd decls of TimeStamp/Version were
  vestigial and deleted; its `static const VERSION` / `s_instance` members became inline-variable
  definitions in the .inl. With this, **no known GMF-poison headers remain** — every module-blocked
  conversion (IncludeProcessing next) has an import to reach for. Exercised at every app/test start
  (the settings file loads through the inlined TOML path).
- **Phase 3 (cont.) — `IndexerCommandCxx` joins `:tooling`; `FilePathFilter` joins `srctrl.file`. ✅**
  The Cxx payload + its Clang-backed static helpers convert (.cpp inlined; the memoized
  `getMacOSSysrootPath` local statics are safe — an inline function's local statics are one entity).
  Three findings this forced:
  - **`FilePathFilter` had to modularize first** (IndexerCommandCxx members) — it couples FilePath
    with Qt (QRegularExpression), so it joins `srctrl.file` with the Qt TYPES arriving via a new
    `import srctrl.qt` (Qt must NOT enter srctrl.file's GMF — that would break `import std`
    coexistence, the whole reason srctrl.qt exists). Two macro/operator seams: `QStringLiteral`
    (a macro, can't cross the import) became plain `QString` construction, and Qt's free
    `operator+`(QString,QString) is NOT re-exported by `export using ::QString` — member
    `prepend`/`append` instead.
  - **finding (a) strikes via forward declarations in GMF headers**: `utilitySourceGroupCxx.h`
    fwd-declares `class FilePath`, which clashes with `import srctrl.file` — and a GMF header whose
    *signatures* use FilePath fundamentally cannot coexist with the import (the GMF is preprocessed
    before the import). Fix: split `CdbLoadError`/`loadCDB` out into `CdbLoad.h/.inl` (born
    dual-build, purview-included by :tooling); utilitySourceGroupCxx.h re-includes it for its
    classic consumers.
  - A purview fwd-decl of `clang::tooling::CompilationDatabase` would likewise attach a bogus
    entity — the header's clang fwd-decl block is purview-guarded, the real decls come from the
    wrapper's Clang GMF.
- **Phase 3 (cont.) — `:parser` (visitor-layer slice 1): the AST-support layer. ✅** CanonicalFilePathCache,
  the utilityClang helpers, and the full name_resolver/ family (CxxNameResolver + Decl/Type/Specifier/
  TemplateArgument/TemplateParameterString) converted into `srctrl.cxx:parser` (~1.6k LOC inlined);
  `ParseLocation` joined `srctrl.data:location`, `FileRegister`+`utilityFile` joined `srctrl.file`,
  and `SymbolKind` — evidently missed when its siblings converted — joined `srctrl.data:types` on the
  way. Hard-won findings:
  - **The 6-way mutually recursive resolver family needs the Edge/Node discipline generalized**: an
    include-guard cycle means a naive per-header `.inl` include parses bodies before sibling classes
    are complete. Solution: an **apex pattern** — non-apex headers bottom-include the apex
    (CxxDeclNameResolver.h, whose top pulls the deepest class chain); only the apex bottom-includes
    `CxxNameResolverBodies.h`, which completes the remaining siblings and then includes all six
    `.inl`s. Any classic entry point converges there with every class complete; the module wrapper
    orders headers-then-inls explicitly and never enters.
  - **Automated requalification needs a first-party guard**: a compiler-error-driven loop that
    qualifies bare names (from deleted `using namespace clang/std` directives, per ADR-0005) must
    skip first-party identifiers, or it happily prefixes `clang::` onto them forever. Also: a driver
    TU that itself has `using namespace clang` *masks* unqualified names in the `.inl`s it includes —
    pick a clean driver.
  - Same Qt/locale `toLowerCase` seam as FileSystem: CanonicalFilePathCache now uses a local ASCII
    helper (paths; mirrors FilePath::getLowerCase()).
  Verified both modes — the CxxParser suite (283 cases) exercises the resolvers directly.
  **Remaining for Phase 3:** the visitor cluster itself (CxxAstVisitor + ~10 components,
  CxxIndexingContext/CxxSymbolRegistry, recorders, PreprocessorCallbacks, CxxParser/IndexerCxx —
  ~4.4k LOC), which should join `:parser`/`:tooling`-style clusters.
- **Phase 3 (cont.) — visitor enablers: `ParserClient` → `srctrl.data:parser`; `ReferenceKind` →
  `:types`. ✅** The recording interface every language indexer writes through is a pure abstract
  class whose parameter types were all already modularized — it becomes the new (aptly named)
  `srctrl.data:parser` partition. `ReferenceKind` converts via the AccessKind/SymbolKind enum
  pattern. The remaining lib_core surface of the visitor cluster is GMF-safe as-is
  (`IndexerStateInfo` — a 10-line plain struct — and `utilityMainFunction`, std-only). The visitor
  cluster itself is now unblocked; its own hazards, scouted: `CxxAstVisitor.cpp` carries
  `using namespace clang` over 887 lines (requalify first, per ADR-0005), and the CRTP visitor +
  variadic component tuple mean the class-definition ordering needs care (components before
  visitor, `CxxIndexingContext`/`CxxSymbolRegistry` before components).
- **Phase 3 (cont.) — lib_cxx is ADR-0005-clean; the visitor blob is scoped for one shot.** All five
  remaining `using namespace clang/std` TUs requalified (CxxAstVisitor — clean immediately, the
  directive was vestigial — plus the Declaration/Type/Reference indexer components and
  CxxDiagnosticConsumer, compiler-loop-driven with the hardened rules: uppercase-only auto-guess,
  once-per-site memo; lowercase identifiers are locals and must never be namespace-prefixed).
  **Finding: the visitor cluster is one strongly-connected blob** — the recorders need
  `CxxAstVisitorComponentContext` complete, every component body includes `CxxAstVisitor.h`, and the
  visitor's tuple holds all components by value. It cannot convert piecewise: one `:visitor`
  partition, all ~15 files at once (SymbolRegistry, IndexingContext, LocationExtractor, recorders,
  Component base + 10 components, CxxAstVisitor), with `CxxAstVisitor.h` as the classic-build apex
  (it already includes every component header — the natural `Bodies.h` site). The frontend glue
  (CxxParser, IndexerCxx, actions, PreprocessorCallbacks, CxxDiagnosticConsumer) can follow as a
  separate thin slice on top.
- **Phase 3 (cont.) — `:visitor` landed: the blob converted in one shot. ✅** All 16 files
  (CxxLocationExtractor, CxxSymbolRegistry, the two recorders, CxxIndexingContext,
  CxxAstVisitorComponent + 9 components, CxxAstVisitor — ~4.5k LOC) inlined into `.inl`s and joined
  as `srctrl.cxx:visitor` (imports `:name`/`:context`/`:parser` + srctrl.data/file/utility). The
  apex pattern generalized cleanly, with one refinement over the resolver family:
  - **Bottom-include placement rule**: a blob header may bottom-include the apex ONLY if no other
    blob header top-includes it. Interior headers (Component base ← the 9 components; recorders +
    SymbolRegistry ← IndexingContext) must NOT — their bottom-include would fire the apex while the
    includer's class is still mid-parse, and the apex needs that class complete (guard-skip ≠
    complete). The safe set converges via the apex's own top (which pulls every blob header);
    `CxxAstVisitorBodies.h` at the apex bottom then parses all 16 `.inl`s with everything complete.
  - **Exported-fwd-decl rule (new)**: in the purview, EVERY fwd decl of a partition-local class
    must carry `SRCTRL_EXPORT`, not just the first — clang 22 rejects an exported definition whose
    immediately-previous redeclaration is unexported ("cannot export redeclaration ... previous
    declaration is not exported"), so a plain `class X;` between the exported first decl and the
    exported definition breaks the chain.
  - **`utilityMainFunction` was NOT GMF-safe** (previous scouting called it std-only): it
    fwd-declares `NameHierarchy`, and a GMF fwd decl of a modularized class clashes exactly like a
    full include (finding (a)). Fixed by modularizing it into `srctrl.data:name` (dual-build
    `.h`/`.inl`; statics → `utility_main_function_detail` inline consts; ADR-0005 requalified) and
    moving the SearchMatch overload of `isMainFunction` to `SearchMatch.h/.inl` (`:search`) where it
    belongs — its one consumer (UndoRedoController) repointed.
  - The vestigial anonymous namespace in CxxAstVisitor.cpp (verbose-log helpers) became
    `cxx_ast_visitor_detail` (ODR trap, as always).
  **Cost: `srctrl.cxx-visitor.pcm` = 130.9 MB** — the biggest BMI yet (RecursiveASTVisitor + CFG in
  the GMF); compile time remains fine, confirming disk-not-time. Verified both modes: 2640
  assertions, headless Usages index 529 nodes / 0 errors, identical either way. **Remaining for
  Phase 3:** the frontend glue thin slice (CxxParser, IndexerCxx, ASTAction/ASTConsumer/
  GeneratePCHAction, PreprocessorCallbacks, CommentHandler, CxxDiagnosticConsumer), then Phase 4
  (indexer binary as pure importer).
- **Phase 3 (cont.) — `:frontend` landed: the glue slice. lib_cxx's parser pipeline is now fully
  modular. ✅** The 10 frontend pairs (CxxParser, ASTAction/ASTConsumer/GeneratePCHAction/
  SingleFrontendActionFactory, PreprocessorCallbacks, CommentHandler, CxxDiagnosticConsumer,
  CxxCompilationDatabaseSingle, ClangInvocationInfo — ~1.3k LOC) became `srctrl.cxx:frontend`
  (imports `:tooling`/`:parser`/`:visitor` + settings/file/process/data; Frontend/Driver/Tooling
  GMF, 150.0 MB BMI). Unlike the visitor blob this layer is a clean DAG — no apex needed; every
  header bottom-includes its own `.inl` (the CanonicalFilePathCache leaf pattern). On the way:
  - **`Parser` (the 15-line abstract base) joined `srctrl.data:parser`** — it fwd-declared
    ParserClient, so it was never GMF-safe (same lesson as utilityMainFunction).
  - **IndexerCxx deliberately stays a classic TU**: it drags the lib_core indexer framework
    (Indexer<T> → ParserClientImpl → IntermediateStorage), which is Phase 4's subject. Classic TUs
    including dual-build headers keep working, so nothing blocks on it.
  - **Qt's `emit` macro vs clang's Sema.h**: with the glue headers self-contained, any including TU
    now mixes Qt-bearing first-party headers (ApplicationSettings/ResourcePaths) with
    Sema-reaching clang headers, and the old per-TU `push_macro("emit")` sandwiches
    (SourceGroupCxxCdb, utilitySourceGroupCxx) can no longer cover the collision (Sema.h has an
    `emit()` member). Fix: `QT_NO_EMIT` on `Sourcetrail_lib_cxx` and `Sourcetrail_test` (both
    non-GUI, zero Qt-keyword uses) and the sandwiches deleted.
  - **Latent visitor-slice bug surfaced and fixed**: the 42 `DEF_VISIT_*` X-macro-generated
    `CxxAstVisitor::Visit*` bodies in CxxAstVisitor.inl lacked `inline` — invisible while only ONE
    classic TU (ASTConsumer.cpp) included the apex, duplicate symbols the moment the glue made it
    several. Moral: after inlining a `.cpp`, grep the result for macro-GENERATED definitions too.
  - Same Qt/locale `toLowerCase` seam as FilePath/FileSystem/CFPC: `cxx_parser_detail` gained an
    ASCII `toLowerCaseAscii` for the cl.exe/clang-cl driver-mode check.
  - The dead `TaskParseCxx` friend/fwd in CxxParser.h removed.
  Verified both modes: 2640 assertions, headless Usages index 529 nodes / 0 errors, identical.
  **Remaining for Phase 3: nothing.** lib_cxx is converted end-to-end (`:name`/`:context`/
  `:tooling`/`:parser`/`:visitor`/`:frontend`) except IndexerCxx + the project/ source-group layer,
  which belong to Phase 4 (indexer framework + binary as pure importer) and the IncludeProcessing
  side quest.
- **Phase 3 — `srctrl.cxx`.** Modularize `lib_cxx` with partitions; absorb the Clang-header BMI cost.
  **STARTED — `:name` landed. ✅** The `name/` cluster (CxxName type-erased wrapper + concept, the five
  decl-name leaves, CxxQualifierFlags — 7 headers, all `.cpp`s inlined into `.inl`s) is `srctrl.cxx`'s
  first partition, and `Sourcetrail_lib_cxx` gained the same dual-build CMake block as lib_core
  (FILE_SET, `-fno-modules-reduced-bmi`, `CXX_MODULE_STD` gate). Chosen precisely because it is
  Clang-free: the module bootstraps without paying the Clang-header BMI cost. Cross-module imports:
  `srctrl.utility` (utilityEnum flag operators, utilityString join), `srctrl.data` (NameHierarchy);
  GMF keeps only std + the P2988 `stdcompat/optional` shim. Verified both modes (2640 assertions,
  identical headless Usages index — the CxxParser suite exercises these classes directly).
  Next slices, in rough dependency order: `utility/` (IncludeDirective/IncludeProcessing,
  CompilationDatabase), `data/indexer/` (IndexerCommandCxx payload — Clang-free header), then the
  Clang-facing parser layers (`CxxContext`/`CxxSymbolRegistry`/visitor components) where the
  Clang-header GMF cost finally lands and needs measuring.
- **Phase 3 (cont.) — the Clang BMI cost MEASURED via `:context`. ✅** `CxxContext` (a single
  using-alias over `llvm::PointerUnion<const NamedDecl*, const Type*>`) converted as the deliberate
  probe: minimal purview, full clang/AST GMF. **Numbers (clang 22, -fno-modules-reduced-bmi, debug):
  `srctrl.cxx-context.pcm` = 41.8 MB; partition compiles in ~2.8 s — cheaper than a comparable classic
  Clang TU (~4.0 s). The cost is disk, not time,** and it amortizes: importers of future Clang-facing
  partitions skip re-parsing the Clang headers entirely. Caveats recorded: (a) full-BMI mode inflates
  every unit (`:name` is 22.3 MB with only std+NameHierarchy in its GMF) — revisit when the LLVM
  #166068 fix ships and reduced BMI returns; (b) if many Clang-facing partitions each carry their own
  AST GMF the disk cost multiplies — prefer ONE Clang-bearing partition cluster (or a shared internal
  partition) when converting the visitor layers.
  **Also scoped and deferred with reasons:** `IncludeProcessing`/`CompilationDatabase`/
  `IndexerCommandCxx` impls all drag the non-modularized lib_core mid-layer (ApplicationSettings,
  MessageStatus/messaging, ResourcePaths, ToolChain) into any wrapper GMF — and those textually
  include FilePath.h, which clashes with `import srctrl.file`. They wait for either that mid-layer's
  modularization or an impl split (payload vs. static toolchain helpers for IndexerCommandCxx).
- **Phase 3 (cont.) — blocker map CORRECTED + `:tooling` partition (CompilationDatabase). ✅**
  The deferral analysis above over-counted: a non-modularized header is GMF-poison **only if it
  transitively includes a modularized header**. Re-derived: `ToolChain.h` (std-only), the messaging
  core (`Message/MessageBase/MessageQueue` → Id/TabIds/types only), `utilitySourceGroupCxx.h`
  (std+clang), and `TextCodec.h` (Qt+std) are all **GMF-safe as-is** — a module TU may GMF-include
  them and link their classic impls (global-module entities keep normal mangling). The only real
  blockers left: `utilityApp.h` and `ApplicationSettings.h` (both textually reach FilePath.h).
  Consequences banked:
  - `ToolChain.cpp` requalified per ADR-0005 (its file-scope `using namespace std/string_literals/
    utility` was the obstacle to ever inlining it; all redundant `""s`/`""sv` suffixes dropped).
  - **`utility::CompilationDatabase` converted** into the new **`srctrl.cxx:tooling`** partition —
    the designated Clang-tooling cluster (per the :context measurement, Clang-bearing code joins
    THIS partition instead of opening new ~40 MB GMFs; :tooling's BMI is 29.8 MB). First partition
    importing three first-party modules (srctrl.utility + srctrl.file + srctrl.logging) alongside a
    Clang GMF.
  Remaining for the deferred pair: `IndexerCommandCxx` needs `utilityApp` modularized (uses
  `utility::executeProcess`); `IncludeProcessing` needs settings.
- **Phase 2 (cont.) — `srctrl.process` (utilityApp). ✅** Its own module rather than a partition: it
  is the one mid-layer piece coupling Qt (QProcess) with FilePath, so it fits neither srctrl.file
  (deliberately Qt-free) nor srctrl.qt (a pure Qt value-type wrapper). Conversion hazards handled:
  the file-scope `using namespace std::chrono` dropped (ADR-0005), the `constexpr` timeout constants
  became `inline constexpr` (namespace-scope constexpr has internal linkage, which a module cannot
  export), the mutable running-process registry became `inline` variables (one instance across TUs),
  and the anonymous-namespace helpers moved to a named `utility_app_detail` namespace (an inline
  function referencing internal-linkage entities is an ODR trap). A vestigial ResourcePaths.h
  include dropped. Verified: the headless index *exercises* the inlined `executeProcess` for real —
  it is what spawns the indexer subprocess. **This unblocks `IndexerCommandCxx`** (all of its impl
  deps now import or GMF-include cleanly); `IncludeProcessing` still waits on settings.
- **Phase 4 — the indexer binary.** `src/indexer/main.cpp` becomes a pure consumer:
  `import srctrl.cxx;` (+ optionally `import std;`).
  **STARTED — `srctrl.indexer` (the framework core) + IndexerCxx landed. ✅** The language-agnostic
  framework — IndexerCommandType, the type-erased IndexerCommand (+ IndexerCommandC concept),
  IndexerBase (+ IndexerErrorCode/IndexerError), the Indexer<T> template, ParserClientImpl, and
  IndexerComposite — is a NEW standalone module `srctrl.indexer` (39.1 MB BMI), deliberately ABOVE
  srctrl.data and srctrl.storage: the framework spans ParserClient (data) and IntermediateStorage
  (storage), and storage already imports data, so it can live in neither. IndexerCxx then joined
  `srctrl.cxx:frontend` (its base Indexer<IndexerCommandCxx> now imports cleanly). Findings:
  - **A modularized header must leave every wrapper GMF it was in**: `srctrl.cxx:tooling` had
    GMF-included IndexerCommandType.h (fine while classic) — once srctrl.indexer owned the enum,
    the global-module copy clashed in any TU importing both. Fix: the GMF include becomes
    `import srctrl.indexer;`. Grep all `.cppm`s for a header before modularizing it.
  - **The inline-adder heuristic misses free functions without a qualified return type**
    (`IndexerCommandType stringToIndexerCommandType(...)` has no `::` before the paren) — caught as
    a duplicate symbol across LanguagePackage TUs. Same lesson class as the DEF_VISIT_* macros:
    after inlining, verify every col-0 definition really got `inline`.
  - IndexerBase's out-of-line default ctor became in-class `= default` (no key-function concerns:
    all virtuals pure or defaulted); the 3-line IndexerBase.cpp and the header-only anchor
    IndexerCommand.cpp were deleted outright.
  - IndexerStateInfo.h and utilityExpected.h stay deliberately GMF-safe/classic — they're in
    srctrl.cxx's GMFs too, and plain std-only headers cost nothing.
  **Slices 2+3 (same day): the command providers and the language-package registry joined
  srctrl.indexer. ✅** IndexerCommandProvider + Memory/CombinedIndexerCommandProvider (the fan-out
  consumption choke point; needed `import srctrl.utility` for utility::append), then
  LanguagePackage + LanguagePackageManager (singleton slot → inline variable). The per-language
  packages (Cxx/Rust/Swift/Zig) stay classic implementers of the importable interface.
  **Slice 4: the Rust/Swift/Zig payloads joined srctrl.indexer. ✅** Plain-value payloads
  (FilePath-bearing → GMF-unsafe once the codec registry modularizes), converted to guarded
  header + .inl; the wrapper GMF gained `<set>`/`<vector>` (first users in this module). No
  wrapper GMF-included them (grep-all-cppms rule held).
  **Slice 5: `srctrl.interprocess` landed. ✅** The whole shared-memory transport as a NEW
  standalone module above srctrl.indexer + srctrl.storage (12 cpp→inl pairs + ProcessId /
  InterprocessBackend / IndexerCommandCodec header-only): IpcSharedMemory(+GarbageCollector),
  the IpcSerializer stack (garbage-collector / indexer-command / indexing-status /
  intermediate-storage wire forms) + the type-erased codec registry, the three Ipc managers,
  IntermediateStorageChunker, InterprocessIndexer. The flatbuffers-generated wire headers stay
  GMF. ODR care: six named detail namespaces replaced anonymous namespaces / file statics; all
  static members became inline definitions. The process-wide singletons (codec registry's
  function-local static, the live-handle registry, GC s_instance) stay one-per-process across
  classic TUs and the module because exported/global inline entities keep their ordinary
  mangling and dedup at link — same mechanism the whole dual build rests on. Both binaries'
  Ipc test suites (IpcIntegration/IpcSerializer/IpcSharedMemory) pass in both modes.
  **Slice 6: `import thoth.ipc;`. ✅** thoth-ipc upstream added an opt-in sqlpp23-style named
  module (THOTH_IPC_BUILD_MODULES → target `thoth_ipc_module`: the interface unit re-exports
  the header surface from its own GMF, so the entities remain global-module — no attachment
  clash with classic includes). Submodule bumped (0881642); the top-level CMake flips
  THOTH_IPC_BUILD_MODULES with SOURCETRAIL_CXX_MODULES (symmetrically, cache follows both
  ways); lib_core links thoth_ipc_module; srctrl_interprocess.cppm's three thoth GMF includes
  became `import thoth.ipc;`.
  **Remaining for Phase 4 — (c) `src/indexer/main.cpp` as importer. NEEDS A DESIGN PASS
  first; do not attempt mechanically.** The blocker is the *type boundary* between imports and
  the deliberately-classic seam headers, not missing modules: main.cpp's classic includes
  (FileLogger/LogManager, AppPath/UserPaths, setupApp, the lib_cxx prebuild runners,
  LanguagePackageCxx, CxxIndexerCommandCodec) textually declare or drag modularized types —
  FilePath, LanguagePackage, IndexerCommandCodecRegistry. In a TU that ALSO imports
  srctrl.file/indexer/interprocess, the textual declaration (global module) and the imported
  one (attached to a named module) are DIFFERENT front-end types even though exported entities
  mangle identically at link. Any value crossing the boundary breaks: e.g. textual
  `UserPaths::getAppSettingsFilePath()` (global FilePath) into imported
  `ApplicationSettings::load(attached FilePath)`, or textual `LanguagePackageCxx` (derives
  global LanguagePackage) into imported `LanguagePackageManager::addPackage`. Options, in
  rough preference order: (1) modularize the FilePath-bearing seams first (FileLogger/LogManager
  → srctrl.logging; AppPath/UserPaths → srctrl.file or a small srctrl.app; the lib_cxx project/
  layer incl. LanguagePackageCxx + CxxIndexerCommandCodec + prebuild runners → srctrl.cxx),
  after which main.cpp imports everything and includes only macro/glaze/std-only headers;
  (2) an #ifdef-swapped main.cpp that imports only modules whose types never cross into the
  remaining textual includes (currently: none usefully — every import overlaps); (3) leave
  main.cpp classic (works today in both modes) and declare the milestone reached when (1) is
  done. TaskBuildIndex + the queue-fill task stay host-side classic (messaging + scheduling)
  until/unless messaging modularizes.
  **Seam slice LANDED — the lib_core FilePath-bearing seams are gone.** srctrl.logging now owns
  the whole backend (LogMessage / Logger / ConsoleLogger / FileLogger / LogManagerImplementation
  / LogManager singleton → inline member) alongside the srctrl::log facade, held at the BOTTOM of
  the module stack by two seams: **FileLogger is FilePath-free** (std::filesystem::path API —
  srctrl.file imports srctrl.logging, so FilePath here would be a module cycle; the five callers
  pass .getPath()/.string()), and setLoggingEnabled's MessageStatus/Version announcement is an
  out-of-line seam fn (`log_manager_detail::notifyLoggingToggled`: include-free decl in
  LogManagerNotifier.h — GMF-held so the purview include no-ops via its guard — classic def in
  LogManagerNotifier.cpp; messaging stays out of the module graph). **Macro-header pattern for a
  modularized backend:** logging.h keeps only the LOG_* macro definitions in a purview (its guard
  strips the backend includes); wrappers include it AFTER `#define SRCTRL_MODULE_PURVIEW` instead
  of in the GMF, so the expansions name the LogManager imported from srctrl.logging. Sweep
  lessons: srctrl_data-location had never imported srctrl.logging (its LOG uses rode on the GMF
  include) — "declaration must be imported before it is required" at first macro use;
  srctrl_settings GMF-included Logger.h directly (now poison — dropped, ApplicationSettings.inl's
  guarded include + the import cover it); srctrl_file had leeched `<algorithm>` through
  logging.h's backend includes. Also split FilePath-free `setupLocale.{h,cpp}` out of setupApp
  (indexer main includes it; setupApp.h re-includes it for the app/test bootstrap). **Remaining
  for the main.cpp milestone: ONLY the lib_cxx project/ seam** (LanguagePackageCxx,
  CxxIndexerCommandCodec, CxxModulePrebuildRunner/CxxPchBuildRunner) — plus switching main.cpp's
  own LOG_* sites to srctrl::log (an importer TU cannot textually include logging.h's classic
  backend path). GlazeCli.h to be checked for GMF-safety at flip time.
  **PHASE 4 COMPLETE — `srctrl.cxx:package` + main.cpp as pure importer LANDED.** The new
  partition carries the language-package glue (LanguagePackageCxx, registerCxxIndexerCommandCodec
  with its provider in `cxx_indexer_command_codec_detail`, both prebuild runners with the
  scanner/BMI helpers in `cxx_module_prebuild_runner_detail`); it is the ONE srctrl.cxx partition
  importing srctrl.interprocess, keeping that dependency out of the parser pipeline. GlazeCli.h
  proved GMF-safe (std + glaze only). main.cpp imports
  srctrl.file/settings/logging/indexer/interprocess(/cxx under BUILD_CXX_LANGUAGE_PACKAGE) under
  SRCTRL_MODULE_BUILD (same define + per-file CXX_SCAN_FOR_MODULES mechanics as the test target);
  its textual remainder is language_packages.h (macro-only, may precede the imports),
  GlazeCli/setupLocale/utilityExpected + std; LOG_* sites became srctrl::log (classic branch gets
  them from LogFacade.h). QT_NO_EMIT joined the app target (the self-contained package headers
  reach Sema.h from src/app/main.cpp). **Two toolchain rules from the first TU mixing the
  clang-heavy srctrl.cxx BMIs with srctrl.interprocess/thoth.ipc:** (1) EVERY BMI in one import
  graph must be full — thoth.ipc.pcm was the only reduced one (upstream can't know the consumer's
  policy) and crashed clang's ASTReader (`ASTDeclReader::Visit` during std:: lookup) when
  deserialized next to the srctrl.cxx BMIs; the consumer's top-level CMake now sets
  -fno-modules-reduced-bmi (+ the Debug hardening defines) on thoth_ipc_module. (2) In an
  importer TU every #include must PRECEDE the imports — a textual libc++ header parsed after the
  BMI-merged declarations fails with "cannot add 'abi_tag' attribute in a redeclaration" (the
  fwd-header's tagged declaration must come before the class-body friend redecl, and BMI merging
  inverts the order if imports come first). thoth-ipc's module doc states the same rule (GCC
  PR114795 over there); the libc++ ODR signature itself was IDENTICAL across the define sets —
  this is declaration order, not config mismatch. The ON-mode headless index exercises the
  importer-built indexer subprocess end to end.
- **Phase 5 — dogfood.** Index the module-built Sourcetrail *with* the module-built Sourcetrail; diff
  the symbol/edge graph against the header build's graph (they must match) and benchmark incremental
  rebuilds ON vs OFF.
  **Phase 5 SELF-INDEX LANDED (2026-07-21, commits 294f9fe0 + 6ac4ca7c + 54bec574):** the
  module-built Sourcetrail indexed its own modules-ON build via the CMake File API source group
  (`Sourcetrail.srctrl.toml`, preset llvm-clang-dbg): **625 files, ~133k LOC, 0 errors; 42
  NODE_MODULE nodes (every srctrl module + partition, srctrl.ping, thoth.ipc); 140 EDGE_IMPORT
  edges** reproducing the wrapper imports exactly (srctrl.cxx:package → srctrl.interprocess,
  :frontend → srctrl.indexer, importer TUs as file-context → module edges incl. main.cpp's).
  Three indexing defects found+fixed (294f9fe0): modmap path derivation ignored subdirectory
  targets' own build/source dirs + CMake's `..`→`__` object-path mangling (SourceEntry now
  carries target paths, type-safely parsed); the response-file quotes were kept (clang searched
  for module `"name`) and -fmodule-output passed through (an indexing parse must never write
  BMIs into the build tree); CMake's synthesized `@synth_<n>` module-collation targets re-listed
  every interface unit without a modmap → each module TU parsed a second time flag-less → 30
  phantom "module not found" fatals (synth targets skipped now). A thrown nlohmann error during
  development also exposed the headless hang class → fixed fundamentally (ADR-0008: guaranteed
  terminal event via TaskFinally + expectedFromExceptions boundaries, outcome exit codes,
  no-progress watchdog). **Known follow-ups:** module nodes are FLAT (partition/dotted names in
  one element); the `aidkit` module node merges with the `aidkit` C++ *namespace* node (same
  NameHierarchy — module vs namespace name collision); NODE_MODIFIER_EXPORTED shows only 2
  flagged nodes in the self-index (suspiciously few — visitExportDecl coverage to check);
  remaining Phase 5: graph-equivalence diff OFF vs ON and the incremental-rebuild benchmark.
  **PHASE 5 COMPLETE (2026-07-21, commit f01e0c4c):**
  - **Graph equivalence: byte-identical.** Full normalized dumps of the Usages fixture (every
    node with type+modifiers, every edge with endpoints, every node/edge occurrence with exact
    source positions, counts, errors) are identical between the final OFF-built and ON-built
    indexer. Modules change the build, not the semantics — proven at content level, not counts.
  - **Rebuild benchmark (llvm-clang-dbg, M1):** touch FilePath.h → OFF: 298 TUs / 112 s; ON:
    351 TUs / 201 s (~1.8×). Touch a leaf .cpp → 9.1 s both. The dual build makes broad header
    edits DEARER today: classic includers still rebuild AND the BMI chain + importer wrappers
    rebuild on top (serialized by BMI deps). The incremental win arrives only as classic
    includers convert to importers — the benchmark quantifies the current cost honestly and
    should be re-run per consumer-conversion milestone.
  - **Modifier merge fixed:** SqliteIndexStorage::addNodes name-dedup dropped modifiers (type
    was upgraded, modifiers weren't) → NODE_MODIFIER_EXPORTED lost to whichever TU inserted a
    symbol first. Now ORed on merge (+ both-orders regression test). Export status additionally
    recorded per-declaration (Decl::isInExportDeclContext) in the declaration indexer — correct
    for normal module projects where the interface unit claims its own content. Dual-build
    caveat (by construction): classic parses usually claim the shared headers and never see
    `export`, so header-declared exports stay sparse in OUR OWN self-index; the region path
    still covers re-export using-declarations (thoth/Qt re-exports flagged).
  - ~~Deferred module-graph refinements~~ **RESOLVED (2026-07-21): module-graph naming.** Module
    nodes now live in their own `NameDelimiterType::CXX_MODULE` delimiter world (`":"`). Because
    the serialized name embeds the delimiter string, module `foo` and namespace `foo` are
    distinct nodes by construction — the aidkit module/namespace fold is gone, no naming-scheme
    hack needed. Partitions (`primary:part`) are two-element hierarchies: the partition node
    nests under its primary module (MEMBER edge via addNodeHierarchy), the primary is recorded
    as a MODULE node even when only a partition unit is seen, and the displayed qualified name
    joins with `":"` — exactly the C++ spelling. Dots deliberately stay within one element:
    the standard gives them no semantic hierarchy, and inventing a fictitious `srctrl` parent
    module would misrepresent the import graph. Delimiter-sensitive consumers audited: template
    collapse (PersistentStorage) matches CXX by exact string; search rescoring/tooltips use the
    hierarchy's own delimiter; `detectDelimiterType` probes `"::"` before `":"`. Verified E2E on
    a partition+collision fixture (module foo + `export import :part` + exported namespace foo):
    distinct foo nodes (module 8 / namespace 16), MEMBER foo→foo:part, IMPORT foo→foo:part,
    0 errors; Usages unchanged at 529/2041/0 (non-module graphs untouched).
- **Phase 6 — GUI (optional/later).** `lib_gui` + moc, once moc/modules matures.
- **Phase 6 OPENED — the ATTACHMENT PIVOT makes moc coexistence structural. ✅** Two changes:
  (1) `SRCTRL_MODULE_BUILD` is now defined PER SOURCE FILE (CMake, alongside CXX_SCAN_FOR_MODULES)
  in all five targets — it means "this TU imports the module graph", so moc-generated TUs (which
  can never import) and unconverted TUs keep the classic textual view by construction.
  (2) `SRCTRL_EXPORT` = `export extern "C++"` and every wrapper wraps its textual purview in an
  `extern "C++" { }` block: ALL first-party entities attach to the GLOBAL module ([module.unit]/7)
  with ordinary mangling while imports still grant visibility. Motivation: the only alternative
  for Q_OBJECT headers needing module-owned types at parse time was include-after-import, which
  clang documents as unsupported (#61465); with GM attachment, textual parse + import of the same
  entity MERGE (include-before-import direction, guaranteed by the imports-last rule) — verified
  by a standalone smoke test over every SRCTRL_EXPORT declaration shape and by the pilot
  (utilityQt.cpp imports srctrl.file/settings while textually including QtMainView.h, whose
  closure pulls FilePath.h textually AND fwd-declares attached types). Fallout fixed en route:
  free-function .inl definitions were naked-purview (module-attached) against now-GM declarations
  — the purview-wide extern "C++" block covers declarations AND definitions uniformly (36 wrappers
  edited by script); SRCTRL_LOGGING_VIA_IMPORT is unusable when the TU's textual closure itself
  logs (QtMainView.h -> FilePath.inl expands LOG_* at include time) — logging stays fully classic
  there. CONSEQUENCES: the mangling law and the include-only-contract asymmetry are retired; the
  consumer frontier exploded from "exhausted" to 227 CONVERTIBLE / 4 BLOCKED (all four stdexec:
  TaskBuildIndex.h ×3, Schedulers.h) — the Application and storage strata are now consumer-open
  too. Verified dual-mode: ON build + 2656/2656 + Usages 529/2041/0; OFF build + 2656/2656.
- **`srctrl.messaging` LANDED — after solving the "BMI-merge mystery" that briefly parked it. ✅**
  The whole messaging family (MessageBase/Queue/Filter/ListenerBase/Listener mesh + ~96 message
  and filter types + honorary members IndexingOutcome/RefreshInfo/ShardConfig) is a module; the
  slice first shipped OFF-green but died ON with `declaration 'trim' attached to named module
  'srctrl.utility:string' cannot be attached to other modules` in `srctrl_cxx-frontend.cppm` —
  with provably zero textual utilityString parses in the failing TU. **Root cause (ours, not
  clang's):** `srctrl_cxx-tooling.cppm` GMF-pre-loaded `MessageStatus.h` (rule 9, from when it
  was classic); once MessageStatus joined the family, its header pulled `utilityString.h` through
  a `#ifndef SRCTRL_MODULE_PURVIEW` guard that is INACTIVE in a GMF (the macro is defined only
  after `export module`) — global-module attachment vs `import srctrl.utility` = ill-formed,
  [basic.link]/10. Clang's diagnosis is lazy and order-dependent (GM-copy-first deserialization
  → silent DefaultIgnore warning; named-module-first → hard error), which made it look like a
  compiler bug; minimal 6-file reproducer + full LLVM-issue research conserved in
  `tools/modules-migration/repro-gmf-attachment/`. Fixes: :tooling imports srctrl.messaging
  (playbook rule 9 amended: GMF includes need a module-free closure; `convertible.py --audit-gmf`
  enforces it); `-Wdecls-in-multiple-modules` enabled on both module targets to surface the
  silent ordering. **Second seam (new rule 11):** MessageQueue's impl is classic-forever (stdexec
  pimpl), and module attachment is part of a class type's MANGLING — so MessageQueue AND every
  type in its signatures (MessageBase, MessageFilter, MessageListenerBase) are GM-attached via
  purview-gated `export extern "C++" {}` ([module.unit]/7); out-of-class `s_nextId` definitions
  need a plain `extern "C++"` wrap (naked purview definitions get module-attached and clash with
  the GM in-class declaration). Verified: ON build green, 2656/2656 assertions, Usages
  529/2041/0; OFF re-verified green.

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
