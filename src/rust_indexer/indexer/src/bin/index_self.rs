// index_self — runs index_crate() on the indexer crate itself and prints a summary.
// Usage: cargo run --bin index_self

use sourcetrail_rust_indexer_lib::parser;

fn main() {
    // Optional argument: index an arbitrary crate/workspace root instead.
    // `--all-features` widens the load to every Cargo feature (used to
    // measure feature-gated coverage deltas).
    let args: Vec<String> = std::env::args().skip(1).collect();
    let all_features = args.iter().any(|a| a == "--all-features");
    let arg = args.iter().find(|a| !a.starts_with("--")).cloned();
    let crate_root = arg
        .as_deref()
        .map(std::path::Path::new)
        .unwrap_or_else(|| std::path::Path::new(env!("CARGO_MANIFEST_DIR")));
    println!("Indexing: {}", crate_root.display());

    let options = parser::CargoOptions {
        all_features,
        ..parser::CargoOptions::default()
    };
    let storage = parser::index_crate(crate_root, options, |_| {});

    println!(
        "\nResults:\n  files:       {}\n  nodes:       {}\n  symbols:     {}\n  locations:   {}\n  occurrences: {}\n  errors:      {}",
        storage.files.len(),
        storage.nodes.len(),
        storage.symbols.len(),
        storage.source_locations.len(),
        storage.occurrences.len(),
        storage.errors.len(),
    );

    if !storage.errors.is_empty() {
        println!("\nErrors:");
        for e in &storage.errors {
            println!(
                "  [{}] {}",
                e.translation_unit.as_deref().unwrap_or(""),
                e.message.as_deref().unwrap_or("")
            );
        }
    }

    println!("\nNodes (first 60):");
    for node in storage.nodes.iter().take(60) {
        let kind_name = match node.type_ {
            8 => "mod",
            64 => "struct",
            256 => "trait",
            1024 => "const/static",
            2048 => "field",
            4096 => "fn",
            8192 => "method",
            16384 => "enum",
            32768 => "enum-const",
            65536 => "type",
            131072 => "type-param",
            262144 => "file",
            524288 => "macro",
            1048576 => "union",
            _ => "?",
        };
        println!(
            "  [{kind_name:12}] {}",
            node.serialized_name.as_deref().unwrap_or("")
        );
    }
    if storage.nodes.len() > 60 {
        println!("  … and {} more", storage.nodes.len() - 60);
    }
}
