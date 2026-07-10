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
//       EDGE_MEMBER       (1<<0 = 1)   — owner → field/method/assoc item
//       EDGE_TYPE_USAGE   (1<<1 = 2)   — trait bounds on type params
//       EDGE_CALL         (1<<3 = 8)   — call sites, resolved via Semantics
//       EDGE_INHERITANCE  (1<<4 = 16)  — `impl Trait for Type`, supertraits
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

const DEFINITION_EXPLICIT: i32 = 2;
const LOCATION_TOKEN: i32 = 0;
const LOCATION_SCOPE: i32 = 1;

const EDGE_MEMBER: i32 = 1 << 0;
const EDGE_TYPE_USAGE: i32 = 1 << 1;
const EDGE_USAGE: i32 = 1 << 2;
const EDGE_CALL: i32 = 1 << 3;
const EDGE_INHERITANCE: i32 = 1 << 4;

// ---------------------------------------------------------------------------
// Public entry point: index a whole Cargo crate
// ---------------------------------------------------------------------------

/// Index the Cargo crate whose `Cargo.toml` lives in `crate_root`.
/// `on_file` is called with each source file path as it begins processing.
/// Returns a populated `OwnedIntermediateStorage` covering all local source files.
pub fn index_crate(crate_root: &Path, on_file: impl FnMut(&str)) -> OwnedIntermediateStorage {
    let cargo_config = CargoConfig::default();
    let load_config = LoadCargoConfig {
        load_out_dirs_from_check: false,
        with_proc_macro_server: ProcMacroServerChoice::None,
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
    let src_dir = tmp.path().join("src");
    if let Err(e) = std::fs::create_dir_all(&src_dir) {
        return error_storage(file_path, &format!("mkdir src: {e}"));
    }
    let cargo_toml =
        format!("[package]\nname = \"_idx\"\nversion = \"0.0.0\"\nedition = \"2021\"\n");
    if let Err(e) = std::fs::write(tmp.path().join("Cargo.toml"), &cargo_toml) {
        return error_storage(file_path, &format!("write Cargo.toml: {e}"));
    }
    if let Err(e) = std::fs::write(src_dir.join("lib.rs"), &source) {
        return error_storage(file_path, &format!("write lib.rs: {e}"));
    }

    let mut storage = index_crate(tmp.path(), |_| {});

    // Rewrite the synthetic file path back to the original path so callers
    // see the real file path in the storage.
    for f in &mut storage.files {
        f.file_path = file_path.to_owned();
    }
    storage
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
