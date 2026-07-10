// Rust source parser — Phase 3b using ra_ap_hir (rust-analyzer HIR).
//
// Loads a full Cargo crate via ra_ap_load_cargo::load_workspace_at() and
// walks every ModuleDef in every local module to emit an OwnedIntermediateStorage.
//
// What is emitted per definition:
//   - StorageFile  for every source file that contains at least one definition
//   - StorageNode  for each named item (fn, struct, enum, union, trait, type
//                  alias, const, static, macro, module)
//   - StorageSymbol (DefinitionKind = EXPLICIT = 2) for each node
//   - StorageSourceLocation pointing at the name token of the item
//   - StorageOccurrence linking node → location
//   - StorageEdge  for relationships:
//       EDGE_MEMBER        (1<<0 = 1)   — owner → field/method/assoc item/type param
//       EDGE_TYPE_USAGE    (1<<1 = 2)   — type refs; bounds (from the param node)
//       EDGE_CALL          (1<<3 = 8)   — call sites, resolved via Semantics
//       EDGE_INHERITANCE   (1<<4 = 16)  — `impl Trait for Type`, supertraits
//       EDGE_OVERRIDE      (1<<5 = 32)  — impl method → trait method
//       EDGE_TYPE_ARGUMENT (1<<6 = 64)  — generic arguments at use sites
//   (see context/DESIGN_RUST_TYPE_SYSTEM_EDGES.md)
//   - StorageError  for load / analysis failures
//
// Node kind constants mirror NodeKind.h (bitmask values):
//   NODE_MODULE          = 1 << 3  = 8
//   NODE_STRUCT          = 1 << 6  = 64
//   NODE_INTERFACE       = 1 << 8  = 256   (trait)
//   NODE_GLOBAL_VARIABLE = 1 << 10 = 1024  (const / static)
//   NODE_FUNCTION        = 1 << 12 = 4096
//   NODE_METHOD          = 1 << 13 = 8192
//   NODE_ENUM            = 1 << 14 = 16384
//   NODE_TYPEDEF         = 1 << 16 = 65536
//   NODE_TYPE_PARAMETER  = 1 << 17 = 131072
//   NODE_MACRO           = 1 << 19 = 524288
//   NODE_UNION           = 1 << 20 = 1048576
//
// Edge type constants mirror Edge::EdgeType in Edge.h (bitmask values):
//   EDGE_MEMBER       = 1 << 0 = 1
//   EDGE_TYPE_USAGE   = 1 << 1 = 2
//   EDGE_INHERITANCE  = 1 << 4 = 16
//
// DefinitionKind: NONE = 0, IMPLICIT = 1, EXPLICIT = 2
// LocationType:   TOKEN = 0, SCOPE = 1, QUALIFIER = 2

use std::path::Path;

use ra_ap_load_cargo::{LoadCargoConfig, ProcMacroServerChoice, load_workspace_at};
use ra_ap_project_model::CargoConfig;

use crate::ipc::storage::{OwnedIntermediateStorage, OwnedStorageError, OwnedStorageFile};

const NODE_FILE: i32 = 1 << 18;
const NODE_MODULE: i32 = 1 << 3;
const NODE_STRUCT: i32 = 1 << 6;
const NODE_INTERFACE: i32 = 1 << 8;
const NODE_FIELD: i32 = 1 << 11;
const NODE_GLOBAL_VARIABLE: i32 = 1 << 10;
const NODE_FUNCTION: i32 = 1 << 12;
const NODE_ENUM_CONSTANT: i32 = 1 << 15;
const NODE_METHOD: i32 = 1 << 13;
const NODE_ENUM: i32 = 1 << 14;
const NODE_TYPEDEF: i32 = 1 << 16;
const NODE_TYPE_PARAMETER: i32 = 1 << 17;
const NODE_MACRO: i32 = 1 << 19;
const NODE_UNION: i32 = 1 << 20;

const DEFINITION_IMPLICIT: i32 = 1;
const DEFINITION_EXPLICIT: i32 = 2;
const LOCATION_TOKEN: i32 = 0;
const LOCATION_SCOPE: i32 = 1;

const EDGE_MEMBER: i32 = 1 << 0;
const EDGE_TYPE_USAGE: i32 = 1 << 1;
const EDGE_USAGE: i32 = 1 << 2;
const EDGE_CALL: i32 = 1 << 3;
const EDGE_INHERITANCE: i32 = 1 << 4;
const EDGE_OVERRIDE: i32 = 1 << 5;
const EDGE_TYPE_ARGUMENT: i32 = 1 << 6;

// ---------------------------------------------------------------------------
// Public entry point: index a whole Cargo crate
// ---------------------------------------------------------------------------

/// Index the Cargo crate whose `Cargo.toml` lives in `crate_root`.
/// `on_file` is called with each source file path as it begins processing.
/// Returns a populated `OwnedIntermediateStorage` covering all local source files.
///
/// Proc-macro expansion is enabled: the proc-macro server is discovered from
/// the active toolchain's sysroot (`rustup component add rust-analyzer`), and
/// build scripts run via `cargo check` so dependency proc-macro dylibs and
/// OUT_DIR includes resolve. Both degrade gracefully when unavailable.
pub fn index_crate(crate_root: &Path, on_file: impl FnMut(&str)) -> OwnedIntermediateStorage {
    index_crate_with(crate_root, LoadProfile::FULL, on_file)
}

/// Which parts of the (expensive) load machinery to enable.
#[derive(Clone, Copy)]
pub(crate) struct LoadProfile {
    /// Load the toolchain sysroot. Required for ANY macro expansion: without
    /// core in the extern prelude, `#[derive(...)]` does not even resolve.
    /// Sysroot crates themselves are excluded from collection (Lang-origin +
    /// is_local filters).
    pub sysroot: bool,
    /// Spawn rust-analyzer-proc-macro-srv from the sysroot (third-party
    /// derive/attribute macros; builtin derives expand without it).
    pub proc_macro_server: bool,
    /// Run `cargo check` to collect build-script output (OUT_DIR includes,
    /// dependency proc-macro dylib paths).
    pub out_dirs: bool,
}

impl LoadProfile {
    /// Production: everything on, each degrading gracefully when unavailable.
    pub const FULL: Self = Self {
        sysroot: true,
        proc_macro_server: true,
        out_dirs: true,
    };
    /// Dependency-free temp crates (tests, index_file shim): fast.
    pub const FAST: Self = Self {
        sysroot: false,
        proc_macro_server: false,
        out_dirs: false,
    };
    /// FAST plus sysroot, so builtin derives (`Clone`, `Debug`, …) expand.
    /// Only used by macro-expansion tests; allow(dead_code) instead of
    /// cfg(test) so the constant stays part of the non-test crate surface.
    #[allow(dead_code)]
    pub const SYSROOT: Self = Self {
        sysroot: true,
        ..Self::FAST
    };
}

pub(crate) fn index_crate_with(
    crate_root: &Path,
    profile: LoadProfile,
    on_file: impl FnMut(&str),
) -> OwnedIntermediateStorage {
    let cargo_config = CargoConfig {
        sysroot: profile
            .sysroot
            .then_some(ra_ap_project_model::RustLibSource::Discover),
        ..CargoConfig::default()
    };
    let load_config = LoadCargoConfig {
        load_out_dirs_from_check: profile.out_dirs,
        with_proc_macro_server: if profile.proc_macro_server {
            ProcMacroServerChoice::Sysroot
        } else {
            ProcMacroServerChoice::None
        },
        prefill_caches: false,
        proc_macro_processes: 1,
        num_worker_threads: 1,
    };

    let (db, vfs, _proc_macro) =
        match load_workspace_at(crate_root, &cargo_config, &load_config, &|_| {}) {
            Ok(r) => r,
            Err(e) => {
                let mut storage = OwnedIntermediateStorage::default();
                storage.errors.push(OwnedStorageError {
                    id: 1,
                    message: format!("Failed to load crate: {e}"),
                    translation_unit: crate_root.display().to_string(),
                    fatal: true,
                    indexed: false,
                });
                return storage;
            }
        };

    collector::collect_from_db(&db, &vfs, on_file)
}

// ---------------------------------------------------------------------------
// Compat shim: index a single file by wrapping it in a temp crate.
// Used by the existing per-file tests and by main.rs (one command = one file).
// ---------------------------------------------------------------------------

/// Index a single `.rs` file.
/// Creates a minimal temporary Cargo crate, writes the file as `src/lib.rs`,
/// loads it via `ra_ap_hir`, and returns the storage.
///
/// Uses the fast load config (no proc-macro server, no `cargo check`) — the
/// temp crate has no dependencies, and builtin derives (`Debug`, `Clone`, …)
/// expand natively in rust-analyzer without a server.
pub fn index_file(file_path: &str, _module_prefix: &str) -> OwnedIntermediateStorage {
    let source = match std::fs::read_to_string(file_path) {
        Ok(s) => s,
        Err(e) => return error_storage(file_path, &format!("Failed to read file: {e}")),
    };

    // Build a temp crate: Cargo.toml + src/lib.rs
    let tmp = match tempfile::tempdir() {
        Ok(d) => d,
        Err(e) => return error_storage(file_path, &format!("tempdir: {e}")),
    };
    if let Err(e) = scaffold_temp_crate(tmp.path(), &source) {
        return error_storage(file_path, &format!("scaffold temp crate: {e}"));
    }

    let mut storage = index_crate_with(tmp.path(), LoadProfile::FAST, |_| {});

    // Rewrite the synthetic file path back to the original path so callers
    // see the real file path in the storage.
    for f in &mut storage.files {
        f.file_path = file_path.to_owned();
    }
    storage
}

/// Scaffold a minimal temporary Cargo crate in `dir`: a `Cargo.toml`
/// (package `_idx`, edition 2021, no dependencies) and `src/lib.rs`
/// containing `source`.
pub(crate) fn scaffold_temp_crate(dir: &std::path::Path, source: &str) -> std::io::Result<()> {
    let src_dir = dir.join("src");
    std::fs::create_dir_all(&src_dir)?;
    std::fs::write(
        dir.join("Cargo.toml"),
        "[package]\nname = \"_idx\"\nversion = \"0.0.0\"\nedition = \"2021\"\n",
    )?;
    std::fs::write(src_dir.join("lib.rs"), source)
}

fn error_storage(file_path: &str, msg: &str) -> OwnedIntermediateStorage {
    let mut storage = OwnedIntermediateStorage::default();
    storage.next_id = 2;
    storage.files.push(OwnedStorageFile {
        id: 1,
        file_path: file_path.to_owned(),
        language_identifier: "rust".to_owned(),
        indexed: false,
        complete: false,
    });
    storage.errors.push(OwnedStorageError {
        id: 2,
        message: msg.to_owned(),
        translation_unit: file_path.to_owned(),
        fatal: true,
        indexed: false,
    });
    storage
}

mod collector;

pub fn module_prefix_from_path(file_path: &str, source_root: &str) -> String {
    collector::module_prefix_from_path(file_path, source_root)
}

#[cfg(test)]
mod tests;
