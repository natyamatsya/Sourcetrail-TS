# MSVC C++20-modules bring-up — findings log

Companion to `msvc-modules-handoff.typ`. Records what actually happened bringing the modules dual
build up on Windows/MSVC, best-effort, no CI. **macOS clang remains the semantic referee**; nothing
here relaxes the attachment model.

## Environment

| | |
|---|---|
| Toolset | **MSVC 14.51.36231** (VS "18"/2026-gen; `_MSC_VER` 1951, CMake 19.51) — newest installed, pinned via `scripts/win/Init-ModulesEnv.ps1` |
| Generator / tools | Ninja 1.13.2, CMake 4.4.0, vcpkg (submodule) |
| Qt | 6.8.5 `msvc2022_64` (system), found via `CMAKE_PREFIX_PATH` |
| Configure | `cmake --preset windows-msvc-dbg -D SOURCETRAIL_CXX_MODULES=ON` + scoped: `-D BUILD_CXX_LANGUAGE_PACKAGE=OFF -D BUILD_RUST_LANGUAGE_PACKAGE=OFF -D BUILD_UNIT_TESTS_PACKAGE=OFF` |
| Date | 2026-07-22 |

**Scope of this pass:** the Qt-only lower layers (`lib_aidkit`, `lib_core`) bottom-up. `lib_cxx` + the
indexer are deferred (need the `cxx-indexer` vcpkg feature = a multi-hour LLVM build; the handoff flags
that GMF as macOS-only territory anyway).

## Results

- ✅ **Gate:** `C++20 modules: ENABLED` on MSVC 19.51 — no fallback. The existing gate already accepts
  MSVC ≥ 19.36; W0 only pins/records the toolset (§ gate comment in `cmake/SourcetrailCxxModules.cmake`).
- ✅ **`AidKit_lib` builds** — the first real wrapper (`aidkit.cppm`: `extern "C++"` purview block,
  `SRCTRL_EXPORT`, Qt in the GMF), its BMI, P1689 `/scanDependencies`, dyndep, and importer TUs all
  compile and archive. The core attachment machinery works on MSVC.
- ✅ **`thoth_ipc` builds** after two fixes (below).
- ✅ **Phase-0 POC** builds after a portability fix (below).
- ✅ **`import std` works on MSVC with zero patching** — CMake's `CXX_MODULE_STD` built `std.ixx`/
  `std.compat.ixx` and `aidkit.cppm` consumed it. This is the **recommended MSVC mode** (frontier #2).
- ✅ **`lib_core` builds completely on MSVC** (`Sourcetrail_lib_core.lib`, 0 errors) under `import std`
  with the **below-storage frontier cut**: the 26 module units of the utility/qt/logging/file/data/
  settings/view/process layers build as modules; `srctrl.storage` and everything that imports it
  (messaging, indexer, interprocess) + their consumer TUs compile classic. This is the resolution of
  frontiers #3 (Qt QMetaType IFC bug) and Risk 3 (glaze/toml ICE) on Windows — see below.

## Fixes landed

### 1. `srctrl_modules_poc` — non-member `std::string` operator not visible through `import`
`ping_main.cpp` compared `std::string == "literal"` but only `import`ed `srctrl.ping`; the non-member
`operator==` is not exported by the module (its `<string>` is global-module), so MSVC found no operator
(clang/libc++ pulled `<string>` transitively). **Fix:** include `<string>`/`<cstdio>` textually, before
the import (playbook rule 1). Dual-safe; hardens clang too. → `src/cxx_modules_poc/ping_main.cpp`.
**Candidate playbook rule:** consumers that use non-member operators of a std type via a macro/return
must include that std header textually — the type travels through `import`, its free operators don't.

### 2. `logging.h` — `LOG_*_STREAM` macros need `<sstream>` textual in importer TUs
`SqliteDatabaseIndex.cpp` (a converted importer) used `LOG_INFO_STREAM(...)`, which expands to
`std::stringstream __ss__`. `logging.h` gated `<sstream>` behind
`!defined(SRCTRL_LOGGING_VIA_IMPORT)`, so importer TUs never got it — and `srctrl.logging` does not
export std types. clang/libc++ supplied `<sstream>` transitively; MSVC's STL does not. **Fix:** split
the gate — `<sstream>` for every non-purview TU (incl. importers), `LogManager.h` stays import-only.
→ `src/lib_core/utility/logging/logging.h`. One fix covers every stream-macro importer; hardens clang.

### 3. thoth-ipc — MSVC portability (branch `msvc-cplusplus-portability`)
Two distinct root causes, both pre-existing (thoth-ipc had never built on MSVC; its own CMake warns
"THOTH_IPC_BUILD_MODULES on MSVC is untested"):

- **a) `__cplusplus` version gates.** The `thoth_ipc` target compiles without `/Zc:__cplusplus`, so
  `__cplusplus == 199711L`, so `THOTH_IPC_CONSTEXPR_` degraded to `inline`, so `make_align` was not
  `constexpr` → C2131 cascade into the `abi drift` asserts. **Fix:** a `_MSVC_LANG`-aware
  `THOTH_IPC_CPLUSPLUS` macro in `imp/detect_plat.h` used by the version gates (the bundled
  flatbuffers/gtest already use this idiom), plus `/Zc:__cplusplus` in thoth-ipc's MSVC CMake block.
- **b) ABI align class.** The generated `abi_generated.hpp` selects align-dependent constants
  (`chunk_header_size`, `route_elem_size`, `route_ring_size`, `name_golden_ring`) via
  `#if defined(__APPLE__) && defined(__aarch64__)` (align-8) `#else` (align-16). **MSVC x64 has 8-byte
  `max_align_t`** (`long double == double`), so it is align-8 but fell into the align-16 branch →
  genuine `abi drift`. **Fix:** in the generator `tools/abi/src/main.rs`, the align-8 predicate now
  also matches `_MSC_VER` (C++) / `target_env = "msvc"` (Rust); regenerated the C++ and Rust headers
  (not hand-edited). **Known follow-up:** the POSIX-shortened `*_posix` shm-name golden now aliases the
  macOS value under MSVC — wrong in principle but unused on Windows (POSIX shm names are `#if`'d out;
  Windows uses named kernel objects). Formalizing a distinct `windows_x64` abi target
  (align-8 + `shm_name_max=0`) is the proper fix and needs the generator's 2-class emitter generalized.

## Open frontier — Qt d-pointer types in module *interfaces* on MSVC

**Symptom:** compiling `srctrl.file` (the module interface unit) fails in Qt's own
`qshareddata.h(149): C2027 use of undefined type 'QRegularExpressionMatchPrivate'`.

**Mechanism:** `FilePathFilter` has inline members (in `.inl`, so they live in the module purview) that
use `QRegularExpression::match()` → returns `QRegularExpressionMatch` **by value** → instantiates
`QExplicitlySharedDataPointer<QRegularExpressionMatchPrivate>::~dtor`, which needs the **complete**
Private. Qt only forward-declares that Private in the header and defines it (and the special members
that touch it) out-of-line in QtCore. Classic TUs therefore never instantiate it — they link QtCore's
symbols. clang's module emission also defers it. **MSVC reifies inline module-purview members eagerly**,
so it instantiates the dtor against the incomplete Private. The error notes also surfaced
`QTextStreamPrivate` and `QVariant::Private`, so this recurs across Qt d-pointer types — it is
**systemic**, not specific to `QRegularExpression`.

**Why the easy workaround is wrong:** textually including `<QRegularExpression>` in `srctrl.file`'s GMF
would make the Qt types global-module in *both* `srctrl.file` (textual) and `srctrl.qt` (imported) — the
exact `[basic.link]/10` attachment clash the `repro-gmf-attachment` reproducer demonstrates. MSVC's weak
ownership would accept it; **clang rejects it**. So it would break the macOS build — forbidden.

**Candidate strategies (needs a design decision — recurs widely, esp. in `lib_gui`):**
1. **Out-of-line the Qt-d-pointer-using members** into a classic seam TU (playbook rules 10/11): the
   inline body that returns/holds a by-value Qt d-pointer type moves to a `.cpp` compiled classically
   with textual `<QRegularExpression>`, where QtCore's out-of-line symbols are referenced and nothing is
   instantiated in the module interface. Correct but per-site; may be many sites.
2. **Keep Qt-d-pointer-heavy leaf headers/modules classic on MSVC** — the dual build already supports
   per-module / per-TU classic; scope which Qt-touching units stay textual on Windows.
3. **Probe an MSVC knob** (e.g. `/Zc:templateScope`, instantiation options, or an explicit
   `extern template`) that defers the instantiation — least invasive if one exists.

Recommendation: strategy 1 for isolated cases (`FilePathFilter` is one), escalating to strategy 2 for
Qt-widget-dense areas. This is the natural next unit of work and the point where the migration's design
owner should choose the approach.

## Open frontier #2 — MSVC does not merge textual-std with imported-GMF-std (the `import std` case)

**Symptom:** compiling `srctrl.data-location` (module interface) fails deep in `<istream>` with
`C2500 'std::basic_istream' … 'std::basic_ios' is already a direct base class` and a cascade of
`C2535` "member already defined".

**Mechanism:** `srctrl.data-location` `import`s `srctrl.file`, whose GMF includes `<fstream>` — so
`std::basic_istream`/`basic_ostream` are baked into `srctrl.file`'s **BMI** as global-module entities.
`srctrl.data-location`'s own GMF then textually parses `<ostream>`. The design relies on the textual
copy and the BMI copy of these global-module std types **merging** — which clang does and **MSVC does
not**; MSVC treats the second as a redefinition. This is not a per-file bug: it recurs for essentially
any module that imports another first-party module and shares std headers, which is pervasive.

**The fix is a build-model choice, `SOURCETRAIL_CXX_IMPORT_STD=ON`** (this pass ran with it OFF, i.e.
textual std in GMFs). With `import std`, no wrapper textually includes std — every TU gets the single
`std` module — so there is no textual-vs-BMI duplication to merge. The handoff calls MSVC "the best
platform for `import std`" and expects it to need none of the macOS patching (CMake's `CXX_MODULE_STD`
gate covers MSVC). This reframes the MSVC bring-up: **prefer `import std` on Windows** rather than the
textual-std GMF mode the macOS build uses. Validating that is the recommended next step; it likely
clears this entire class at once (and is a cleaner path than patching per-module std overlaps).

## Open frontier #3 — MSVC cannot round-trip a Qt `QMetaType` specialization through an IFC

**Symptom (with `import std` ON, after all the above):** building the `srctrl.storage` partitions fails
importing `srctrl.data`:
`qmetatype.h(2647): fatal error C1116: unrecoverable error importing module 'srctrl.data'. Specialization
of 'QMetaType::fromType' with arguments 'Id'`, with MSVC's own note: *"IFC import detected … please
follow instructions … about providing a repro for modules: https://aka.ms/report-cpp-modules-problem."*

**Nature:** a `QMetaType::fromType<Id>` specialization ends up in `srctrl.data`'s IFC, and MSVC cannot
merge it with the same specialization instantiated textually in the importing TU — the *same*
textual-vs-BMI non-merging that broke std (frontier #2), but for a **Qt template**, so `import std`
does not help. **MSVC explicitly self-reports this as a compiler modules defect.** `Q_DECLARE_METATYPE`
guards (now added to `Id.h`/`TabIds.h`, playbook-compliant and worth keeping) reduce the surface but do
not remove implicit `fromType<Id>` instantiations that come from actual Qt use in module purviews.

**This is where the bring-up currently stops** (~280/329 of the scoped `lib_core` build; everything up
to the storage layer builds under `import std`). Two ways forward, both a design decision:
1. **Metatype isolation** — ensure any `Id`/`TabId` Qt-metatype instantiation lives in exactly one
   classic TU and never crosses a module boundary (analogous to the FilePathFilter seam, but for the
   metatype system; requires a per-module-unit define, e.g. `SRCTRL_MODULE_UNIT`, plus out-of-lining
   the `QVariant<Id>`-style uses). Invasive and not guaranteed to satisfy MSVC.
2. **Report to Microsoft** with a reduced repro (the handoff's escalation path for suspected MSVC bugs)
   and keep the affected leaf modules classic on MSVC until fixed (the dual build already allows this).

Recommendation: file the MSVC repro (it is a genuine compiler bug, self-flagged) and, in parallel, keep
`srctrl.storage` (and any Qt-metatype-crossing module) classic on Windows via the dual build so the rest
of the module graph proceeds.

**Resolution (implemented) — the below-storage frontier cut.** On MSVC the module frontier stops below
`srctrl.storage`: `cmake/SourcetrailCxxModules.cmake` provides `sourcetrail_msvc_filter_module_units`
(drops the storage/messaging/indexer/interprocess wrapper `.cppm`) and `sourcetrail_msvc_filter_importers`
(drops importer TUs that `import` any of those, so they compile fully classic). Both are no-ops off MSVC
and are applied in `src/lib_core`, `src/lib_gui`, `src/app`, `src/indexer` CMakeLists. The dual build's
global-module attachment lets the classic storage code and the module lower layers share ordinary
mangling, so `Sourcetrail_lib_core.lib` links. This is a coherent partial frontier, exactly the handoff's
bottom-up W2 — extend it upward as MSVC (or our workarounds) allow.

## Frontier #4 (Risk 3) — MSVC ICE on glaze/toml++ in an IFC-import TU

Compiling importer TUs that instantiate `glz::write_json` (reached via `ConfigManager::saveJson`,
inline) crashed `cl` with `C1001 Internal compiler error` in `glaze/api/std/variant.hpp`, again with the
`report-cpp-modules-problem` note (IFC import context). This is the handoff's Risk 3 (glaze/toml++ are
too template-heavy for MSVC's modules frontend). **Fix:** a classic seam — `ConfigManager.inl` became
`ConfigManager.cpp` (all its non-template members out-of-line; the two header templates use no glaze), so
glaze/toml instantiate only in that one classic TU, never an IFC-import one. Same pattern as the
FilePathFilter seam; dual-build safe and a no-op for clang.

## How to reproduce this state

```powershell
git submodule update --init --recursive
.\vcpkg\bootstrap-vcpkg.bat -disableMetrics
.\scripts\win\Init-ModulesEnv.ps1
cmake --preset windows-msvc-dbg -D SOURCETRAIL_CXX_MODULES=ON `
  -D BUILD_CXX_LANGUAGE_PACKAGE=OFF -D BUILD_RUST_LANGUAGE_PACKAGE=OFF -D BUILD_UNIT_TESTS_PACKAGE=OFF `
  -D CMAKE_PREFIX_PATH='D:/dev/sdk/qt/qt-6.8.5/6.8.5/msvc2022_64'
cmake --build .build/windows-msvc-dbg --target AidKit_lib          # green
cmake --build .build/windows-msvc-dbg --target Sourcetrail_lib_core # stops at srctrl.file (Qt frontier)
```
