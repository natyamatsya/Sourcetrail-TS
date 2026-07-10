# Design: Rust Project Model Integration via rust-analyzer

**Status: proposal, not yet implemented.** Orthogonal to the parser-layer
work that has since landed (semantic resolution, reference occurrences,
type-system edges — see `context/DESIGN_RUST_TYPE_SYSTEM_EDGES.md` and
`ROADMAP_RUST_INDEXER.md` Phase 7): this document is about *which code gets
loaded* (features, targets, proc-macros), not how loaded code is analyzed.
Note when implementing: the `ra_ap_*` API surface referenced below moves
quickly — re-verify signatures against the pinned version (0.0.341+), e.g.
`LoadCargoConfig` has grown `num_worker_threads` and `proc_macro_processes`.

## Background

When the Rust indexer was first designed for Sourcetrail, the project model
was treated as a thin wrapper: `ra_ap_load_cargo::load_workspace_at()` is
called with a working directory, and the resulting `RootDatabase` is queried
for symbols. The project configuration — which crates to index, which features
are enabled, which targets exist — is entirely implicit, driven by whatever
`cargo metadata` returns for the workspace root.

This works for simple crates but misses a large part of what rust-analyzer's
project model (`ra_ap_project_model`) exposes:

- **Multiple targets per package** — a package can have a `lib`, one or more
  `[[bin]]`, `[[example]]`, `[[test]]`, and `[[bench]]` targets, each with
  its own crate root and feature set.
- **Cargo features** — features gate entire modules and change the symbol set.
  Indexing with the wrong feature set produces an incomplete or misleading
  graph.
- **Workspace members** — a Cargo workspace can contain dozens of packages.
  The current indexer receives one `working_directory` and indexes everything
  reachable from it, with no way to scope to a subset.
- **Proc-macro crates** — proc-macro expansion is currently disabled
  (`ProcMacroServerChoice::None`). The project model knows which crates are
  proc-macro crates and where the compiled `.dylib`/`.so` lives.
- **Build scripts (`build.rs`)** — `cargo metadata` exposes the output of
  build scripts (generated include paths, `cfg` flags). These affect which
  code is compiled and which symbols exist.
- **`cfg` conditions** — `#[cfg(target_os = "linux")]` gates entire symbol
  subtrees. The project model resolves these against the active target triple.

None of these are surfaced to the Sourcetrail user today. The result is that
indexing a real-world Rust workspace (e.g. `ripgrep`, `tokio`, `rustc` itself)
produces a graph that silently omits feature-gated code and conflates symbols
from different targets.

---

## What `ra_ap_project_model` Provides

The `ra_ap_project_model` crate is the layer between raw `cargo metadata` JSON
and the `RootDatabase` that rust-analyzer uses for analysis. Its key types:

### `ProjectWorkspace`

Represents a fully resolved workspace. Created via:

```rust
ProjectWorkspace::load(
    manifest: ManifestPath,        // path to Cargo.toml
    cargo_config: &CargoConfig,    // features, target triple, extra env
    progress: &dyn Fn(String),
)
```

A `ProjectWorkspace` contains:

- `PackageGraph` — all packages and their dependency edges.
- Per-package: `PackageData` with name, version, manifest path, targets.
- Per-target: `TargetData` with kind (`Lib`, `Bin`, `Test`, `Bench`,
  `Example`, `BuildScript`), crate root path, required features.

### `CargoConfig`

Controls how the workspace is loaded:

```rust
pub struct CargoConfig {
    pub features: CargoFeatures,   // All, Selected(Vec<String>), or Default
    pub target: Option<String>,    // e.g. "aarch64-apple-darwin"
    pub extra_env: FxHashMap<String, String>,
    pub invocation_strategy: InvocationStrategy,
    pub invocation_location: InvocationLocation,
    // ...
}
```

`CargoFeatures` is the key field:

```rust
pub enum CargoFeatures {
    All,                           // --all-features
    Selected {
        features: Vec<String>,     // --features foo,bar
        no_default_features: bool, // --no-default-features
    },
}
```

### `CrateGraph` and `CrateId`

`ProjectWorkspace::to_crate_graph()` converts the Cargo model into the
rust-analyzer internal `CrateGraph` — a directed graph where each node is a
`CrateId` with its own `Env`, `CfgOptions`, and `Edition`. This is what gets
loaded into `RootDatabase`.

Each `CrateId` corresponds to one compilation unit (one `(package, target,
feature-set)` triple). A package with `lib` + two `[[bin]]` targets produces
three `CrateId`s.

---

## Current Sourcetrail Integration (gaps)

```text
SourceGroupRust::getIndexerCommands()
  └─ emits one IndexerCommandRust per source group
       └─ working_directory = Cargo.toml directory

sourcetrail_rust_indexer <processId> <uuid> <appPath> <userDataPath> <logFilePath>
  └─ reads IndexerCommandRust from SHM (results cached per working_directory)
       └─ calls index_crate(working_directory)
            └─ load_workspace_at(working_directory, &CargoConfig::default(), ...)
                 └─ indexes ALL targets with DEFAULT features
```

**Gaps:**

1. **No feature selection** — `CargoConfig::default()` uses default features
   only. Feature-gated code is invisible.
2. **No target selection** — all targets (`lib`, `bin`, `test`, `bench`,
   `example`) are loaded into the same `RootDatabase`. Symbol names collide
   when the same identifier exists in both `lib` and a `bin` target.
3. **No workspace scoping** — the user cannot say "index only the `tokio-core`
   package, not all 47 workspace members".
4. **No proc-macro expansion** — `ProcMacroServerChoice::None` is hardcoded.
   Derive macros (`#[derive(Debug)]`, `#[derive(Serialize)]`) are not expanded,
   so the generated `impl` blocks are absent from the graph.
5. **No `cfg` awareness** — the active target triple is the host triple.
   Cross-compilation targets are not supported.
6. **`IndexerCommandRust` carries no project model parameters** — the command
   only contains `working_directory`, `source_file_paths`, and
   `indexed_paths`. There is no way to pass feature flags or target selection
   from the C++ app to the Rust indexer.

---

## Proposed Integration

### Phase 1 — Extend `IndexerCommandRust` with project model parameters

Add fields to `IndexerCommandRust` (C++) and the corresponding FlatBuffers
schema entry so the C++ app can communicate project model choices to the Rust
indexer:

```cpp
class IndexerCommandRust : public IndexerCommand
{
    FilePath m_workingDirectory;
    std::set<FilePath> m_indexedPaths;
    FilePath m_sourceFilePath;

    // New fields:
    std::vector<std::string> m_features;    // empty = default features
    bool m_allFeatures = false;
    bool m_noDefaultFeatures = false;
    std::string m_targetTriple;             // empty = host
    std::vector<std::string> m_targetKinds; // {"lib"}, {"bin:myapp"}, etc.
};
```

Add corresponding fields to `indexer_command.fbs`:

```fbs
table IndexerCommand {
  // existing fields ...
  features: [string];
  all_features: bool;
  no_default_features: bool;
  target_triple: string;
  target_kinds: [string];
}
```

On the Rust side, `main.rs` reads these fields and constructs a `CargoConfig`:

```rust
let cargo_config = CargoConfig {
    features: if cmd.all_features {
        CargoFeatures::All
    } else {
        CargoFeatures::Selected {
            features: cmd.features.clone(),
            no_default_features: cmd.no_default_features,
        }
    },
    target: cmd.target_triple.clone().filter(|s| !s.is_empty()),
    ..CargoConfig::default()
};
```

### Phase 2 — Per-target indexing

Instead of loading the entire workspace into one `RootDatabase` and walking
all crates, load each requested target separately. This avoids symbol
collisions and allows the user to index only the targets they care about.

**New indexer entry point:**

```rust
pub fn index_target(
    working_dir: &Path,
    cargo_config: &CargoConfig,
    target_kinds: &[String],   // e.g. ["lib"], ["bin:myapp"]
) -> IntermediateStorage
```

Implementation:

1. Call `ProjectWorkspace::load(manifest, cargo_config, &|_| {})`.
2. Filter `workspace.packages()` to those matching `target_kinds`.
3. For each matching target, call `load_workspace_at` with a scoped
   `CargoConfig` that restricts to that target's crate root.
4. Walk only the `CrateId`s that correspond to the selected targets.

### Phase 3 — Project model in `SourceGroupSettingsRust`

Expose the project model parameters in the Sourcetrail project settings UI:

```cpp
class SourceGroupSettingsRustEmpty
    : public SourceGroupSettingsWithComponents<
          SourceGroupSettingsWithSourcePaths,
          SourceGroupSettingsWithExcludeFilters,
          SourceGroupSettingsWithSourceExtensionsEmpty>
{
    // New:
    std::vector<std::string> m_features;
    bool m_allFeatures = false;
    bool m_noDefaultFeatures = false;
    std::string m_targetTriple;
    std::vector<std::string> m_targetKinds;  // default: {"lib"}
};
```

The project settings dialog gains a "Rust" tab with:

- **Features** — text field, comma-separated, or "All features" checkbox.
- **Target** — dropdown populated from `cargo metadata` (lib, bin:name, etc.).
- **Target triple** — text field with host triple as default.

### Phase 4 — Proc-macro expansion

`ra_ap_project_model` knows the path to the compiled proc-macro server
(`rust-analyzer-proc-macro-srv`). When it is available, enable it:

```rust
let proc_macro_server = ProcMacroServerChoice::Sysroot;
// or: ProcMacroServerChoice::Path(path_to_server)
```

This requires:

1. Detecting whether `rust-analyzer-proc-macro-srv` is on `PATH` or in the
   active toolchain's `lib/` directory.
2. Passing `ProcMacroServerChoice` through `IndexerCommandRust` (or
   auto-detecting on the Rust indexer side).

With proc-macro expansion enabled, `#[derive(Debug)]` generates a real
`impl Debug for Foo` block that is indexed as an `EDGE_INHERITANCE` edge
between `Foo` and `Debug`. This is the single biggest gap in the current
symbol graph for idiomatic Rust code.

### Phase 5 — Workspace-level project creation

Add a "Cargo Workspace" project type that reads `cargo metadata` once and
auto-generates one `SourceGroupRust` per workspace member:

```text
User: New Project → Rust Workspace → select Cargo.toml
Sourcetrail: runs `cargo metadata --no-deps --format-version 1`
Sourcetrail: creates one source group per package
             with source paths = package root
             with target kinds = ["lib"] (or all, user-selectable)
```

This replaces the current manual "add source paths" workflow for workspaces.

---

## Symbol Graph Improvements Enabled by the Project Model

| Gap today | Fix via project model |
| --- | --- |
| Feature-gated modules invisible | `CargoFeatures::All` or explicit feature list |
| `#[derive(...)]` impls missing | Proc-macro expansion via `ProcMacroServerChoice` |
| `cfg`-gated code invisible | Correct `CfgOptions` from `CargoConfig::target` |
| `bin` and `lib` symbols conflated | Per-target `CrateId` scoping |
| Build-script-generated code missing | `cargo metadata` `build-script-outputs` |
| Cross-compilation targets unsupported | `CargoConfig::target` field |
| Workspace packages not individually selectable | Phase 5 workspace project type |

---

## Interaction with the Existing Indexer

The changes are additive. The existing `index_crate(working_directory)` path
remains as the default (zero-configuration) entry point. The new
`index_target(working_dir, cargo_config, target_kinds)` path is used when the
`IndexerCommandRust` carries non-default project model parameters.

The `IntermediateStorage` wire format is unchanged. The C++ app receives and
stores the results identically regardless of which indexer path was used.

---

## Open Questions

1. **Feature combinatorics** — a crate with N features has 2^N possible
   feature combinations. Indexing all combinations is infeasible. The
   recommended default is `CargoFeatures::All` (equivalent to
   `--all-features`), which gives the maximal symbol set at the cost of
   potentially including mutually exclusive features. This matches what
   rust-analyzer itself does by default.

2. **`build.rs` outputs** — build scripts can generate Rust source files
   (e.g. `include!(concat!(env!("OUT_DIR"), "/generated.rs"))`). These are
   only available after `cargo build` has run. Sourcetrail should detect
   `OUT_DIR` from `cargo metadata` and index generated files if they exist,
   skipping them otherwise.

3. **`cfg` resolution** — `CfgOptions` in rust-analyzer are resolved per
   crate from `cargo metadata`. For the host target this is automatic. For
   cross-compilation targets, the user must supply the target triple and
   Sourcetrail must pass it through to `CargoConfig`. The set of `cfg` flags
   for a given target triple can be obtained via `rustc --print cfg
   --target <triple>`.

4. **Proc-macro server location** — `rust-analyzer-proc-macro-srv` is
   distributed as part of the `rust-analyzer` binary since 2023 (it is a
   subcommand: `rust-analyzer proc-macro`). The indexer should check for
   `rust-analyzer` on `PATH` and use `ProcMacroServerChoice::Path` pointing
   to it, falling back to `ProcMacroServerChoice::None` if not found.

5. **Incremental re-indexing** — the project model changes when `Cargo.toml`
   or `Cargo.lock` changes (new dependencies, changed features). Sourcetrail
   should watch these files and trigger a full re-index when they change,
   similar to the CMake File API change detection proposal.
