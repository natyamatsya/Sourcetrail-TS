// Build script: runs `flatc --rust` on the Sourcetrail IPC schemas and writes
// the generated code to $OUT_DIR.  The indexer crate includes! it from there.
//
// flatc search order:
//   1. FLATC env var (explicit override)
//   2. PATH
//   3. vcpkg build tree relative to the repo root

use std::path::{Path, PathBuf};
use std::process::Command;

fn find_flatc() -> Option<PathBuf> {
    if let Ok(p) = std::env::var("FLATC") {
        let p = PathBuf::from(p);
        if p.is_file() {
            return Some(p);
        }
    }

    if let Ok(output) = Command::new("flatc").arg("--version").output() {
        if output.status.success() {
            return Some(PathBuf::from("flatc"));
        }
    }

    // CARGO_MANIFEST_DIR = src/rust_indexer/indexer/
    // repo root is three levels up
    let manifest = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap());
    let root = manifest.join("../../..");
    let candidates = [
        root.join("vcpkg/packages/flatbuffers_arm64-osx/tools/flatbuffers/flatc"),
        root.join("vcpkg/packages/flatbuffers_x64-osx/tools/flatbuffers/flatc"),
        root.join("vcpkg/packages/flatbuffers_x64-linux/tools/flatbuffers/flatc"),
        root.join("vcpkg/buildtrees/flatbuffers/arm64-osx-rel/flatc"),
        root.join("vcpkg/buildtrees/flatbuffers/x64-linux-rel/flatc"),
    ];
    for c in &candidates {
        if c.is_file() {
            return Some(c.clone());
        }
    }

    None
}

fn run_flatc(flatc: &Path, schema: &Path, out_dir: &Path) {
    println!("cargo:rerun-if-changed={}", schema.display());
    let status = Command::new(flatc)
        .args(["--rust", "--gen-all", "--gen-object-api", "-o"])
        .arg(out_dir)
        .arg(schema)
        .status()
        .unwrap_or_else(|e| panic!("failed to run flatc on {}: {e}", schema.display()));
    assert!(
        status.success(),
        "flatc failed with status {status} on {}",
        schema.display()
    );
}

fn main() {
    println!("cargo:rerun-if-env-changed=FLATC");

    let manifest = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap());
    let schema_dir = manifest.join("../../../abi-schemas/ipc-indexer");
    let out_dir = PathBuf::from(std::env::var("OUT_DIR").unwrap());

    let schemas = [
        "indexer_command.fbs",
        "intermediate_storage.fbs",
        "indexing_status.fbs",
        "garbage_collector.fbs",
    ];

    let flatc = match find_flatc() {
        Some(p) => p,
        None => {
            // Emit a placeholder so the crate still compiles but panics at
            // runtime if IPC types are actually used.
            let placeholder = out_dir.join("ipc_schemas_generated.rs");
            std::fs::write(
                &placeholder,
                "compile_error!(\"flatc not found — set the FLATC env var or install flatbuffers.\");\n",
            )
            .unwrap();
            println!("cargo:warning=flatc not found; IPC schema types will not be generated");
            return;
        }
    };

    for schema in &schemas {
        run_flatc(&flatc, &schema_dir.join(schema), &out_dir);
    }
}
