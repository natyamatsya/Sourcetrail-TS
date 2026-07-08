# Topic Map & Upstream-Sync Strategy

## Purpose

This fork (`natyamatsya/Sourcetrail`, branch `develop`) carries **168 commits** on top of
Peter Most's upstream (`petermost/Sourcetrail`). This document maps those commits to
coherent topics, records how tightly they are interwoven, and defines the workflow for
staying in sync with upstream and forward-porting individual topics.

It is descriptive, not a plan of record — regenerate the numbers with the commands in the
[Reproducing this analysis](#reproducing-this-analysis) section if the branch moves.

## Fork geometry (as of 2026-07-04)

- **Last common commit (fork point):** `2c0b0418` — *"logic: Use `em` for the `View` buttons size"* (upstream, 2026-02-11)
- **Fork ahead:** 168 commits · **Upstream ahead:** 78 commits (upstream tip `master@upstream`, 2026-06-30)
- Tooling: repo is **colocated jj + git** (`jj git init --colocate`). `trunk()` resolves to `master@upstream`.

## Headline finding: the history does not support clean topic extraction

The 168 commits were authored **interleaved**, not in per-topic blocks, and they mutate a
small set of shared "hotspot" files over and over. Empirically, lifting any single topic out
of the stack produces conflicts. Four extraction experiments confirmed this:

| Experiment | Result | Root cause |
|---|---|---|
| 7 `clang-compat` commits → `trunk()` | 7/7 conflict | **Semantic divergence** — upstream `fad7b92ca` ("Updates for LLVM 21.1.2", which credits @natyamatsya) *removed* multi-LLVM code paths; the fork *added* a multi-version `ClangCompat` layer. Opposite designs. |
| 7 `clang-compat` commits → fork point | 6/7 conflict | Commits #2+ depend on intermediate `cxx:` commits that co-edit `utilityClang.cpp`. |
| 18-commit C++/parser theme → fork point | 15/18 conflict | Threaded through root `CMakeLists.txt` (touched by 8 themes) + other hotspots. |
| 3 `utility-fixes` commits → `trunk()` | 3/3 conflict | The glob fix builds on the earlier cross-cutting `4e2e0c91` QRegularExpression migration. |

**Conclusion:** treat topics below as an *orientation map and upstreaming backlog*, not as
branches to be cut mechanically. The single `develop` stack stays the source of truth.

### Coupling hotspots (files touched by ≥4 topics)

```
8  CMakeLists.txt
7  src/lib/data/indexer/TaskBuildIndex.cpp
6  src/lib_gui/qt/project_wizard/content/QtProjectWizardContentSelect.cpp
6  src/lib/data/indexer/interprocess/serialization/IndexerCommandSerializer.cpp
6  src/lib/settings/ProjectSettings.cpp
5  src/lib/settings/{LanguageType,source_group/SourceGroupType}.{cpp,h}
5  src/lib/data/indexer/interprocess/*  (InterprocessIndexer, IpcInterprocessIndexerCommandManager)
```

## Topics (15) and how separable they are

Two tiers. **Tier A** touches mostly isolated files and is a plausible upstreaming backlog.
**Tier B** is one deeply-coupled "indexer / IPC / project-model" supercluster — keep it as a
stack; if ever upstreamed, do it as a *linear* chain in the dependency order shown, not as
parallel branches.

### Tier A — peripheral, candidate for forward-porting
| Topic | Commits | Notes |
|---|--:|---|
| `build-vcpkg-deps` | 15 | Boost-dropping, vcpkg, CMake presets/toolchains. Overlaps existing `improvement/drop-more-boost`. |
| `clang-version-compat` | 7 | `lib_cxx` Clang 22/23 shims. **Diverges from upstream's LLVM-21 consolidation — needs a design call.** |
| `cxx-modules` | 5 | C++20 modules / compiler resource-dir resolution. Fused with `clang-version-compat` in practice. |
| `utility-fixes` | 3 | glob→regex, symlink handling, CLI11 parsing. |
| `language-removal` | 2 | Drop Java/Maven. Early, mostly deletions. |
| `sqlite-to-duckdb` | 2 | SQLite WAL/pragma tuning (DuckDB roadmap since removed). |

### Tier B — the coupled core (keep as one stack; dependency order →)
```
boost-ipc-to-cppipc (18)  →  indexer-runtime (13)  →  xml-to-toml (14)
   →  cmake-file-api (21)  →  rust-indexer (37)  →  swift-indexer (4)
   →  viz-responsiveness (5)  →  gui-misc (13)     [tests-misc (7) cross-cuts]
```

Full per-commit lists are in the [Appendix](#appendix-full-commit-listing-per-topic).

## Workflow 1 — Sync `develop` onto updated upstream (the primary flow)

This is where jj earns its keep: rebase the whole stack at once and resolve conflicts
in place, commit-by-commit, without the stash/pop dance.

```sh
jj git fetch --remote upstream          # pull upstream/master
jj rebase -d 'trunk()'                   # replay all 168 commits onto the new upstream tip
# jj records conflicts *inside* commits instead of halting; resolve at your leisure:
jj resolve                               # or edit conflict markers directly
jj log                                   # 'jj log' shows which commits are still (conflict)
jj git push --remote origin              # push develop back to the fork  (only when ready)
```

Tips:
- Resolve the **Tier-B core in dependency order** (IPC → indexer-runtime → settings → …) so
  each resolution stabilizes the files the next commit touches.
- Expect the real friction at the hotspot files above, especially anything under
  `src/lib/data/indexer/interprocess/` and root `CMakeLists.txt`.

## Workflow 2 — Forward-port a topic as a clean branch (net-diff, isolated worktree)

Do **not** cherry-pick historical commits (they carry interleaved dependencies), and do
**not** stage the port through the main working copy (its `.build/` artifacts and the fork's
`.gitignore` pollute the commit). Instead, net-diff the topic's files against upstream tip in
a throwaway git worktree:

```sh
WT=$(mktemp -d)
git worktree add -q --detach "$WT" upstream/master
( cd "$WT"
  git checkout develop -- <files-for-this-topic>     # bring in the fork's final version
  git commit -m "topic: <summary> (forward-port onto upstream tip)"
  git branch -f topic/<name> HEAD )
git worktree remove "$WT"
jj git import                                          # make jj aware of the new branch
```

Only files with **zero upstream drift** port as a true no-op merge. Check first:
```sh
git rev-list --count 2c0b0418..upstream/master -- <file>   # 0 == clean; >0 == needs reconciliation
```

### Worked example — already created: `topic/filepathfilter-qregex`
Ports `src/lib/utility/file/FilePathFilter.{cpp,h}` (the std::regex→QRegularExpression glob
migration) onto upstream tip. Both files have 0 upstream drift, so it is a clean net diff
(40+/73−). Branch exists locally; **not** raised as a PR.

Sibling files in `src/lib/utility/file/` were deliberately excluded: `FilePath.cpp`,
`FileSystem.cpp`, `FileSystem.h`, `utilityFile.cpp` have upstream drift (upstream added
`weakly_canonical`, a `FileSize` alias, empty-path fixes) and need manual reconciliation.

## ⚠ Upstream divergences to reconcile before upstreaming

- **LLVM/Clang versioning** — upstream `fad7b92ca` consolidated to a single LLVM 21.1.2 path;
  the fork's `clang-version-compat` + `cxx-modules` build a multi-version abstraction. Pick one
  direction before porting either topic.
- **`FileSystem.cpp`** — upstream and fork both evolved it post-fork; forward-port needs a merge.

## Reproducing this analysis

```sh
MB=2c0b0418                                            # fork point
git rev-list --count $MB..develop                      # fork-ahead count
git merge-base develop upstream/master                 # recompute fork point
# per-topic classification lives in the appendix; it was derived from each commit's
# file footprint + subject. Re-derive touched dirs with:
for c in $(git rev-list $MB..develop); do git show --name-only --format= $c; done \
  | sort | uniq -c | sort -rn
```

## Appendix: full commit listing per topic

Ordered oldest→newest within each topic. Hashes are 9-char prefixes on `develop`.

### rust-indexer (37)
- `fdf99ae7b` feat: remove support for java
- `367a85b9f` feat: add Rust indexer skeleton (Phase 1)
- `9e8217ab4` feat: wire Rust indexer into C++ build system (Phase 2)
- `5bf74163f` fix: preserve original command type in Rust indexer write-back; glob CMakeLists
- `c7e1ff9bc` feat: wire SourceGroupSettingsRustEmpty into ProjectSettings (Phase 3 cont.)
- `503ceac1d` test: add parser unit tests, FlatBuffers round-trip tests, and CI workflow (Phase 6)
- `335cb2144` docs: mark completed phases in ROADMAP_RUST_INDEXER.md
- `a4821b7e6` chore: fetch libipc from github.com/natyamatsya/thoth-ipc (rev 32b772d)
- `0d0bba1b3` feat(parser): upgrade to ra_ap_hir for full HIR-based analysis
- `faa414ca7` feat(main): switch to index_crate() for crate-level HIR indexing
- `8dad16ca7` feat: add lib target + index_self binary; indexer indexes itself
- `f1d504b0c` docs: update ROADMAP_RUST_INDEXER.md — mark Phase 3b complete
- `73319a049` docs: add ROADMAP_PROC_MACRO_EXPANSION.md
- `4b17fdfff` feat(parser): emit EDGE_INHERITANCE and EDGE_TYPE_USAGE edges
- `9674dadcb` fix: wire Rust indexer IPC end-to-end
- `2a1e79b62` fix: eliminate all Rust compiler warnings
- `ad0c3b0d6` refactor: remove Code::Blocks source group support
- `fd8f13107` feat: wire CMakeFileAPIReader into SourceGroupCxxCMakeFileAPI
- `159910f5d` refactor: make InterprocessIndexer::work() return std::expected and improve exception hand
- `f65fa2837` refactor: make Indexer::index() return std::expected and add utilityExpected helpers
- `0a6c0f77c` ipc: add dynamic shared memory growth and lock-free peek to prevent deadlocks
- `14456fd7c` indexer: remove multi-process indexing toggle and improve rust support
- `70b00ef59` test: add Rust indexer command deduplication test for TaskFillIndexerCommandsQueue
- `a0e102827` rust: update IPC shared memory sizes and storage queue wire format to match C++ changes
- `f2f2ff3e9` rust: update Rust indexer dependencies and add shared memory growth support
- `ce3536327` refactor: rename language_packages.h to language_package_flags.h and add constexpr boolean
- `5aed98a04` refactor: remove BUILD_RUST_LANGUAGE_PACKAGE and BUILD_SWIFT_LANGUAGE_PACKAGE preprocessor
- `0c96f5057` refactor: remove BUILD_CXX_LANGUAGE_PACKAGE preprocessor guards from language and source g
- `1a7e23f5b` refactor: remove BUILD_CXX_LANGUAGE_PACKAGE preprocessor guards and convert to constexpr i
- `a771e8e9e` refactor: add Rust indexer consecutive failure tracking and improve Clang compatibility la
- `ee286e73d` refactor: add type parameter and trait member indexing with qualified naming and lifetime 
- `44b4b7249` refactor: add type and const generic parameter member node creation with type usage edges
- `786969075` refactor: add Rust indexer collector with type parameter, trait bound, and impl relationsh
- `e4d96687a` refactor: add exception handling and mutex timeout protection to indexer IPC layer
- `e38200beb` refactor: add struct field, enum variant, and impl method indexing with EDGE_MEMBER relati
- `95f734784` refactor: add call edge indexing with NameHierarchy serialization and node_ids HashMap for
- `050254451` refactor: add per-file progress callback to Rust indexer and fix GraphController character

### cmake-file-api (21)
- `63ea1307f` build: enable BUILD_RUST_LANGUAGE_PACKAGE in sourcetrail-presets
- `6492e0c20` build: move binaryDir into repo at .build/{presetName}
- `a57df4b1e` fix: Catch2 test discovery fails with locale-formatted rng-seed
- `c6df725cd` feat: integrate qt-json-query and implement CMakeFileAPIReader
- `eb48c3ca0` feat: add CMake preset discovery and auto-resolution of build directory
- `a99b1bb38` feat: add CMake File API staleness detection via isReplyStale()
- `6a8af6a4f` feat: add CMake File API project wizard page
- `0f27d711a` refactor: switch CMakeLists files to GLOB_RECURSE + CONFIGURE_DEPENDS
- `f5ea1d0e3` cmake: parse frameworks and split compile command fragments into tokens
- `6b2d36a76` cmake: parse CXX_MODULES file sets and prevent duplicate source entries
- `d1775832d` cmake: add compiler path and sysroot support from toolchains-v1 API
- `11f73856a` cxx: fix C++20 module support and compiler-aware header resolution
- `077259b8c` cmake: shorten CMakePresets.json preset names for brevity
- `d742f4a5e` cmake: split clang presets into apple-clang and llvm-clang variants
- `c7697e20d` cmake: configure llvm-clang preset to use libc++ with system archiver tools
- `a2b24f209` cmake: extract llvm-clang preset configuration into dedicated toolchain file
- `872151573` cmake: add getSourcesDetailed with typed errors and warnings to CMakeFileAPIReader
- `ed922e749` project: add BuildModelSnapshot for structured build system metadata capture
- `6c8ec790c` gui: add QtJsonTreeModel for hierarchical JSON visualization with lazy loading and file re
- `b054b4f6f` cmake: add target dependency tracking to BuildModelSnapshot and CMakeFileAPIReader
- `1a4ce1892` cmake: aggregate target normalization warnings and add target tree filtering with path act

### boost-ipc-to-cppipc (18)
- `d41965618` docs: Add roadmap for migrating from boost::interprocess to thoth-ipc with FlatBuffers
- `e96b143bb` refactor: Add FlatBuffers dependency and schema compilation infrastructure for IPC migrati
- `1c5453ed5` refactor: Add thoth-ipc library integration with IpcSharedMemory wrapper and comprehensive t
- `481e20892` refactor: Add FlatBuffers serialization layer for IPC data structures
- `e11430d99` refactor: Add compile-time backend selection for IPC with thoth-ipc/FlatBuffers support
- `92212f82e` refactor: Add IPC integration test suite for thoth-ipc backend workflow validation
- `052500fea` docs: Update README formatting and add IPC backend performance comparison
- `7862bcde6` refactor: Remove boost::interprocess backend and make thoth-ipc + FlatBuffers the default IP
- `ba0d5fa08` docs: Remove completed boost::interprocess to thoth-ipc migration roadmap
- `84dfcaa77` fix: link External_lib_ipc against 'ipc' target instead of hardcoded .a path
- `966ae5609` fix: increase IPC command queue shm from 1 MB to 64 MB; log overflow errors
- `d23d51a08` fix: InterprocessIndexer waits for commands instead of exiting immediately
- `8e3a5efed` fix: add queue_stopped IPC signal so indexer subprocesses exit cleanly
- `9fbd03509` fix: align queueStopped IPC signal with command queue lifecycle
- `6abb701fe` test: add Swift indexer command skip test for IpcInterprocessIndexerCommandManager
- `fbdb0c619` ipc: refactor shared memory growth to use template-based handle abstraction
- `5d6e23dc1` refactor: add logging to IPC command operations and fix minor code quality issues
- `066fc0422` refactor: conditionally compile CXX test suites and replace preprocessor guards with const

### build-vcpkg-deps (15)
- `654f71b29` build: Remove Qt dependencies and clean up vcpkg.json formatting
- `ca486a1f5` build: Update Clang version support and simplify LLVM header path handling
- `6d63a3339` refactor: Add error code handling to boost::filesystem operations in FileSystem
- `3844f7a53` refactor: Replace boost::filesystem with std::filesystem throughout FilePath and FileSyste
- `f7e270887` build: Remove boost::filesystem dependency and enable std::filesystem in boost::dll
- `1937d9726` refactor: Remove BOOST_DLL_USE_STD_FS compile definition from External_lib_boost target
- `4b6c895b3` refactor: Replace boost::chrono with std::chrono throughout utility and GUI components
- `bf7a2e866` refactor: Replace boost::process and boost::asio with QProcess for process execution
- `ff7734bb6` refactor: Replace boost::date_time with std::chrono throughout TimeStamp and file system c
- `658c9eae0` refactor: Replace boost::program_options with CLI11 for command-line argument parsing
- `edcecaa84` refactor: Replace boost::predef with standard C++ preprocessor macros for platform and arc
- `b9ce6b9e4` refactor: Remove ICU dependency from build configuration
- `d7a7d5cae` refactor: Migrate AidKit tests from GTest to Catch2
- `eeac9475d` cmake: add C++20 module interface support and prevent duplicate language flags
- `d41519697` cmake: add xcrun fallback for macOS sysroot when not provided by CMake

### xml-to-toml (14)
- `d81f75522` doc: Add roadmaps for SQLite to DuckDB and XML to TOML migrations
- `15a4913ae` build: add toml++ support and guard Rust files behind BUILD_RUST_LANGUAGE_PACKAGE
- `68f6325ea` feat(config): add TOML read/write support with .srctrl.toml extension
- `3a9a459b7` feat(config): Phase 5 — new projects use .srctrl.toml by default
- `68c691249` feat(config): Phase 7 — deprecation warning when loading legacy .srctrlprj
- `a163640a0` chore(tools): add XML to TOML project conversion script
- `9ac09ca04` fix: replace cmake -N subprocess with CMakePresets.json parsing in resolveBinaryDir
- `b00eed346` settings: seed default application values on load
- `d182fd4f5` cxx: inject bundled clang resource-dir and clear argument adjusters to fix builtin header 
- `3d17ed0a7` cxx_modules: add CMakePresets.json and update Sourcetrail config to version 8
- `98e8e7126` cmake: add nlohmann-json dependency and replace Qt JSON parsing with nlohmann/json in CMak
- `9b36b719a` project: legacy cleanups & new .srctrl.* suffixes
- `74817f743` project: remove Java source group support and clean up project configurations
- `8943cc3d5` fixup!

### gui-misc (13)
- `4e2e0c91d` refactor: Replace std::regex with QRegularExpression across file path filtering and search
- `23e56a367` feat(wizard): implement New Rust Project source group workflow
- `7697ac4d2` Stabilize indexing diagnostics and reduce subprocess noise
- `d5f815421` project: add getSourceGroups() accessor and integrate Build JSON Browser into View menu
- `73ff8a55d` gui: replace Qt JSON types with nlohmann::json in QtJsonTreeModel and add root normalizati
- `0bbac8316` gui: add search functionality to QtBuildJsonBrowser with progress tracking and auto-expans
- `8155f4b34` gui: add Targets tab to Build Browser with hierarchical target/file tree and detailed insp
- `9e930cc94` gui: add dependency target navigation in Build Browser with click-to-jump functionality
- `c87712895` gui: add file-level compile details panel with tabbed view for includes, defines, and flag
- `552c02460` settings: remove multi-process indexing configuration option from ApplicationSettings and 
- `f9db2a5c2` refactor: remove BUILD_RUST_LANGUAGE_PACKAGE and BUILD_SWIFT_LANGUAGE_PACKAGE preprocessor
- `a3b59689c` refactor: move C++ command deserialization into switch statement and fix include indentati
- `a1b808931` refactor: replace enum scope qualifiers with using enum declarations across Qt view and co

### indexer-runtime (13)
- `c787cb9a3` feat: add Rust language package, source group, and factory (Phase 3)
- `6f86d0e17` feat: C++ indexer skips Rust commands; add .rs default extensions (Phase 4)
- `cab83df37` fix: emit one IndexerCommand per Rust source group (crate-level indexing)
- `5fe27c7fc` fix: teardown crash in 'can create application instance' test
- `828ee429c` indexer: improve diagnostics and multiprocessing stability
- `46b0bbdd8` config: deduplicate missing-key warnings
- `fa2d82d68` fix: wrap indexer->index() in try-catch and prevent duplicate crash marks
- `aed72b249` refactor: improve indexer process error handling and failure diagnostics
- `78a4996ea` refactor: verbose indexer process error output formatting
- `e4a73a2cf` indexer: fix storage manager selection logic for Swift and Rust processes
- `6fd1e73f4` indexer: add Swift indexer command deduplication support to TaskFillIndexerCommandsQueue
- `3346e9c04` refactor: extract language package registration and source group creation into template he
- `7d1113c22` refactor: extract Rust and Swift command deduplication into template helper function

### tests-misc (7)
- `42c28d461` !fixup
- `47febb596` feat: Extract non-type template parameter names from DeclRefExpr in template arguments
- `e2376faa3` test: add empty compiler path parameter to IndexerCommandCxx test constructors
- `f924b5dfc` test: add Swift indexer command deduplication test for TaskFillIndexerCommandsQueue
- `f97c11388` refactor: add TaskBuildIndex integration test for Swift subprocess workflow
- `6fa974a58` refactor: conditionally compile SourceGroupCxxTestSuite and extract CXX test utilities int
- `436d98c12` refactor: extract Rust and Swift test utilities into OptionalRustTestUtils and OptionalSwi

### clang-version-compat (7)
- `767a9c802` refactor: Replace IntrusiveRefCntPtr with direct DiagnosticOptions usage and update API ca
- `d1165a1ee` refactor: update Clang compatibility from version 22 to 23
- `bf62261ea` refactor: add Clang compatibility layer for API changes between versions
- `f38cc7b61` refactor: add Clang compatibility layer for NestedNameSpecifier API changes between versio
- `14464f0d6` refactor: rename ClangCompatibility to ClangCompat and reorganize compatibility layer stru
- `57e01b785` refactor: remove legacy ClangCompatibility shim files and CMake filter
- `22d889811` refactor: add Identifier case to NestedNameSpecifierKind enum and handle Identifier and El

### viz-responsiveness (5)
- `751864bf2` refactor: Replace boost::uuid with QUuid for UUID generation
- `bb283e6a5` refactor: Replace boost::locale with Qt and std::locale for string operations and locale s
- `85724e09e` docs: add visualization responsiveness roadmap for large projects
- `9f096c3fb` feat: remove Visual Studio source group support
- `1cf76fbd0` refactor: replace enum scope qualifiers with using enum declarations across controller and

### cxx-modules (5)
- `a1fd911b4` cxx: inject bundled clang resource-dir in ClangInvocationInfo and CxxParser via ArgumentsA
- `bbb0dac24` cxx: centralize compiler resource-dir resolution and fix system include injection logic
- `997ce4a14` cxx: add compiler invocation fallback and caching to resolveCompilerResourceDir
- `62ab4a0ae` cxx: use C++20 using enum declarations in utilityClang switch statements
- `e6680e880` cxx: unconditionally inject system includes and use default compiler for resource-dir reso

### swift-indexer (4)
- `80d6e94b7` swift: add Swift language package support with external indexer process
- `362151f04` swift: add Swift language package infrastructure with empty source group support
- `ed521e809` swift: add FlatBuffers schema generation and Swift Package Manager integration
- `dee83bf0e` swift: add IPC channel infrastructure for command, status, and storage communication

### utility-fixes (3)
- `7ad9c2068` refactor: Simplify glob pattern to regex conversion with character-by-character processing
- `e4027f1eb` refactor: Improve symlink handling and error code management in FileSystem directory itera
- `5769aad68` refactor: Fix CLI11 argument parsing and add expected value counts for boolean options

### language-removal (2)
- `85138037b` refactor: Remove Java/Maven configuration options from CommandlineCommandConfig
- `37686d862` refactor: remove Java and Maven dead code

### sqlite-to-duckdb (2)
- `5c7dacb43` docs: Remove SQLite to DuckDB migration roadmap
- `71cb6ad6f` refactor: Optimize SQLite performance with WAL mode and tuned pragmas

### OTHER (1)
- `d996fa4ef` fix: disable non-trivial destructor calls and reduce diagnostic verbosity

### docs (1)
- `12301f250` fixup!
