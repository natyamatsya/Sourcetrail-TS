// index_self — runs index_crate() on the indexer crate itself and prints a summary.
// Usage: cargo run --bin index_self

use sourcetrail_rust_indexer_lib::parser;

fn main() {
    let crate_root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
    println!("Indexing: {}", crate_root.display());

    let storage = parser::index_crate(crate_root, |_| {});

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
            println!("  [{}] {}", e.translation_unit, e.message);
        }
    }

    println!("\nNodes (first 60):");
    for node in storage.nodes.iter().take(60) {
        let kind_name = match node.type_ {
            8 => "mod",
            64 => "struct",
            256 => "trait",
            1024 => "const/static",
            4096 => "fn",
            8192 => "method",
            16384 => "enum",
            65536 => "type",
            131072 => "macro",
            262144 => "union",
            _ => "?",
        };
        println!("  [{kind_name:12}] {}", node.serialized_name);
    }
    if storage.nodes.len() > 60 {
        println!("  … and {} more", storage.nodes.len() - 60);
    }
}
