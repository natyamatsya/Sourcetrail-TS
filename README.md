# Sourcetrail

Sourcetrail is a free and open-source cross-platform source explorer that helps you get productive on unfamiliar source code. It is:

* Free
* Working offline
* Supporting C/C++, with additional out-of-process language packages for Rust, Swift, and Zig (see [Language Packages](#language-packages))
* Operating on Linux, Windows (and macOS)
* Offering a SDK ([SourcetrailDB](https://github.com/CoatiSoftware/SourcetrailDB)) to write custom language extensions

Sourcetrail is licensed under the [GNU General Public License Version 3](LICENSE.txt).

!["Sourcetrail User Interface"](docs/readme/user_interface.png "Sourcetrail User Interface")

## Important

This project was archived by the original authors and maintainers of Sourcetrail by the end of 2021. You can read more about this decision in this [blog entry](https://web.archive.org/web/20211115131149/https://www.sourcetrail.com/blog/discontinue_sourcetrail/).

This repository is an **experimental fork** — a personal playground for exploring new indexers (Rust, Swift, Zig), IPC backends, and multi-process indexing fan-out. It is published as-is, purely for anyone curious about that experimentation. There are **no plans for community support**: no issue triage, no roadmap, no release or stability guarantees, and history may be force-rewritten without notice.

At a high level it explores polyglot out-of-process indexers (Rust, Swift, Zig), a C++20-modules migration, a `thoth-ipc`/FlatBuffers IPC backend, concurrent Turso storage, multi-process indexing fan-out, and an agent/MCP bridge — see [Contributions](#contributions) for detail.

If you need a **stable, production-grade Sourcetrail**, please use the actively maintained [**petermost/Sourcetrail**](https://github.com/petermost/Sourcetrail) fork that this work builds upon. It has kept Sourcetrail buildable and dependable for production use for many years since the original project was archived, and offers prebuilt binaries. If you'd like to support that effort, please consider sponsoring its maintainer at <https://github.com/sponsors/petermost>.

## **Contents**

* [Quick Start Guide (Version 2021.4)](DOCUMENTATION.md#getting-started)
* [Documentation (Version 2021.4)](DOCUMENTATION.md)
* [Contributions](#contributions)
* [Language Packages](#language-packages)
* [Building](#building)

# Dependencies

Dependencies are provided by **vcpkg** — see [`vcpkg.json`](vcpkg.json) for the exact set and the pinned `builtin-baseline`. The main libraries are FlatBuffers, glaze, nlohmann-json, SQLite3, sqlpp23, tomlplusplus, stdexec, and Catch2 (tests), plus Clang/LLVM for the C/C++ indexer — built via vcpkg on Windows, system-provided on Linux/macOS. Qt 6 and the per-language toolchains (Rust, Swift, Zig) come from the system.

# Contributions

Since diverging from upstream in February 2026 (~590 commits), the study has explored the
following, grouped by subject. These are exploratory technology studies, not release-hardened
features.

**Language packages** (out-of-process indexers over a shared FlatBuffers wire ABI, [`abi-schemas/`](abi-schemas/README.md))
* **Rust** — macro-usage edges (derive/attr/bang), lifetime use-sites, implicit
  generic-specialization nodes, `#[deprecated]`/`#[cfg]` metadata, function-local symbols.
* **Swift** — parity series SW7–SW16: global-actor/`Sendable` concurrency, attached &
  freestanding macros, property wrappers & result builders, protocol conformance, generics.
* **Zig** — a new out-of-process Zig indexer wired into the app.

**C++20 modules migration** (the largest effort, ~80 commits)
* Progressively converting the codebase to C++20 modules (`import std`), including the Qt/moc
  coexistence and the glaze/toml compiler seams.
* Windows bring-up under both MSVC (`cl.exe`) and `clang-cl`; clang-cl builds the full module graph.

**Indexing fan-out & execution**
* Distributed sharded indexing: per-source-group subprocess clusters, crate/package-granular
  fan-out for Rust & Swift, a type-erased `IndexerCommand` codec, chunked oversized storage pushes.
* Headless watchdog, terminal events and exit codes; raised then removed the subprocess cap.
* Concurrency modernization: stdexec senders/receivers with a Qt scheduler bridge, retiring
  `QtThreadedFunctor` across ~20 components.

**Storage** (Turso + SQLite)
* Concurrent Turso sole-writer with MVCC conflict-retry and a separate SQLite export.
* A firewalled Rust shim (`tsq_*`) over `turso_core`, a `CppTurso3` C++ wrapper, and migration
  of the SQLite storage layer to sqlpp23.

**IPC backend**
* Replaced `boost::interprocess` with `thoth-ipc` + FlatBuffers (now the sole backend), with an
  event-driven command queue over a cross-process condition variable. See
  [IPC Backend Performance](#ipc-backend-performance).

**Agent control & MCP bridge**
* An agent-UI control channel (over thoth-ipc): a `GetInfo` handshake, `QueryUi` (server-side
  JSONPath element selection), `CaptureElement` screenshots, `InvokeAction`, UI-tree snapshots,
  event subscription, and a multi-instance app pool.
* A Rust MCP bridge exposing the running app to AI agents.

**Configuration**
* Settings and color schemes migrated from XML to JSON (via glaze); project files moved to TOML
  (`.srctrl.toml`); the TinyXML dependency removed.

**Windows & build**
* CMake File API support for MSVC projects, a vcpkg overlay for LLVM, and directory-junction
  based test data (no symlink privileges needed).

For the pre-fork history of the lineage this builds on, see the upstream
[petermost/Sourcetrail](https://github.com/petermost/Sourcetrail) README.

# IPC Backend Performance

The legacy `boost::interprocess` indexer backend was replaced by `thoth-ipc` + FlatBuffers,
now the default and only IPC implementation. Against the old backend, the migration measured:

| Metric | boost::interprocess (old) | thoth-ipc + FlatBuffers | Delta |
|---|---|---|---|
| **Wall time** | 5.56s | 3.57s | **-36%** |
| **User time** | 3.48s | 0.89s | **-74%** |
| **Sys time** | 1.25s | 0.29s | **-77%** |
| **RSS** | 10.2 GB | 113 MB | **-99%** |
| **Indexing time** | 4.66s | 1.85s | **-60%** |

Measured on macOS (release build) indexing the tictactoe sample project (7 source files, 1069 nodes, 7556 edges).

# Language Packages

Each language is indexed by a *language package*, gated behind a CMake option (all
default `OFF`; the presets enable C/C++, Rust, and — on macOS — Swift):

| Option | Language | Extra tools required |
|--------|----------|----------------------|
| `BUILD_CXX_LANGUAGE_PACKAGE`   | C/C++ | Clang/LLVM (vcpkg or system) |
| `BUILD_RUST_LANGUAGE_PACKAGE`  | Rust  | Rust toolchain (`cargo`) |
| `BUILD_SWIFT_LANGUAGE_PACKAGE` | Swift | Swift toolchain |
| `BUILD_ZIG_LANGUAGE_PACKAGE`   | Zig   | `zig` 0.16 and `flatcc` (`brew install zig flatcc`) |

The C/C++ indexer is built into the app; Rust, Swift, and Zig run as separate indexer
processes that communicate with the app over a shared FlatBuffers wire ABI defined in
[`abi-schemas/`](abi-schemas/README.md). Enable a package at configure time, e.g.:

```
cmake --preset rel -DBUILD_ZIG_LANGUAGE_PACKAGE=ON
```

# Building

All builds use the bundled **vcpkg** to provide dependencies, driven by CMake presets —
there is no separate system-package build path. Pick a preset for your platform and
compiler, configure, then build into `.build/<preset>`.

## Cloning

Clone the repository and run the init script:

```
git clone https://github.com/natyamatsya/Sourcetrail-TS.git
cd Sourcetrail-TS
python init_repository.py
```

`init_repository.py` initializes the submodules and bootstraps vcpkg; re-run it after a
pull that moved submodules. On **Linux/macOS** you may add `--config core.symlinks=true`
to the clone. On **Windows** no symlink support is needed — the build uses directory
junctions, which require no special privileges or Developer Mode.

Update with:

```
git pull --recurse-submodules
python init_repository.py
```

## Prerequisites

* **Linux:** run `scripts/install-vcpkg-dependencies.sh` (system libraries vcpkg needs to
  build the Qt package), plus a system Clang/LLVM for the C/C++ indexer.
* **Windows:** [Visual Studio 2026](https://visualstudio.microsoft.com/vs/community/) with
  the C++ toolset. Build from a "x64 Native Tools Command Prompt for VS 18", or run
  `scripts/win/Init-ModulesEnv.ps1` first.
* **macOS:** [Xcode](https://developer.apple.com/xcode/) command-line tools, plus
  autoconf, autoconf-archive, automake, libtool, and ninja.

## Configure and build

Choose a preset:

| Preset | Platform / compiler |
|--------|---------------------|
| `rel` / `dbg` / `reldbg`             | Linux; system Clang/LLVM for the C/C++ indexer |
| `rel-cxx`                            | as `rel`, but builds LLVM via vcpkg for the C/C++ indexer |
| `apple-clang-rel` / `llvm-clang-rel` | macOS (also enables the Swift package) |
| `windows-msvc-rel`                   | Windows, MSVC (`cl.exe`) |
| `windows-clang-cl-rel`               | Windows, `clang-cl` |

The presets enable the C/C++ and Rust language packages and the unit tests by default;
enable others with e.g. `-D BUILD_ZIG_LANGUAGE_PACKAGE=ON` (see
[Language Packages](#language-packages)).

```
cmake --preset rel
cmake --build .build/rel
```

The first configure builds the vcpkg dependencies — and, for `rel-cxx`, LLVM — which takes
a **long** time.
