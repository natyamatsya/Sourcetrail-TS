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
