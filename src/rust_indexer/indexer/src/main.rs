// sourcetrail_rust_indexer
//
// Launched by the Sourcetrail app with positional CLI arguments:
//   <processId> <instanceUuid> <appPath> <userDataPath> <logFilePath>
//
// Mirrors the behaviour of InterprocessIndexer.cpp:
//   1. Open the three IPC channels (command, status, storage).
//   2. Poll for Rust-typed IndexerCommand entries.
//   3. For each command: update status → index file → push storage → finalise status.
//   4. Exit when the queue is empty or indexing_interrupted is set.

use std::thread;
use std::time::Duration;
use std::{collections::HashMap, path::Path};

use sourcetrail_rust_indexer_lib::ipc::{CommandChannel, StatusChannel, StorageChannel};
use sourcetrail_rust_indexer_lib::parser;

fn main() {
    env_logger::init();

    let args: Vec<String> = std::env::args().collect();

    let process_id: u64 = args.get(1).and_then(|s| s.parse().ok()).unwrap_or(0);
    let uuid = args.get(2).cloned().unwrap_or_default();
    let _app_path = args.get(3).cloned().unwrap_or_default();
    let _user_data_path = args.get(4).cloned().unwrap_or_default();
    let _log_file_path = args.get(5).cloned().unwrap_or_default();

    if uuid.is_empty() {
        eprintln!("sourcetrail_rust_indexer: missing instanceUuid argument");
        std::process::exit(1);
    }

    log::info!("process_id={process_id} uuid={uuid} starting");

    let cmd_ch = match CommandChannel::open(&uuid) {
        Ok(c) => c,
        Err(e) => {
            log::error!("Failed to open command channel: {e}");
            std::process::exit(1);
        }
    };
    let status_ch = match StatusChannel::open(&uuid, process_id) {
        Ok(c) => c,
        Err(e) => {
            log::error!("Failed to open status channel: {e}");
            std::process::exit(1);
        }
    };
    let storage_ch = match StorageChannel::open(&uuid, process_id) {
        Ok(c) => c,
        Err(e) => {
            log::error!("Failed to open storage channel: {e}");
            std::process::exit(1);
        }
    };

    // Many Rust commands can point to the same crate root. Cache the indexed
    // crate result and reuse it to avoid repeated rust-analyzer workspace loads.
    let mut storage_cache: HashMap<
        String,
        sourcetrail_rust_indexer_lib::ipc::storage::OwnedIntermediateStorage,
    > = HashMap::new();

    loop {
        // Check for interrupt before doing any work.
        match status_ch.is_interrupted() {
            Ok(true) => {
                log::info!("indexing interrupted — exiting");
                break;
            }
            Err(e) => log::warn!("status read error: {e}"),
            _ => {}
        }

        // Pop the next Rust command.
        let cmd = match cmd_ch.pop_rust_command() {
            Ok(Some(c)) => c,
            Ok(None) => {
                // Queue empty — we are done.
                log::info!("command queue empty — exiting");
                break;
            }
            Err(e) => {
                log::error!("Failed to pop command: {e}");
                break;
            }
        };

        log::info!("indexing \"{}\"", cmd.source_file_path);

        // Back-pressure: wait until the app has consumed enough results.
        loop {
            match storage_ch.storage_count() {
                Ok(n) if n < 2 => break,
                Ok(n) => {
                    log::info!("waiting — {n} storages pending");
                    thread::sleep(Duration::from_millis(200));
                }
                Err(e) => {
                    log::warn!("storage count error: {e}");
                    break;
                }
            }
            // Re-check interrupt while waiting.
            if matches!(status_ch.is_interrupted(), Ok(true)) {
                log::info!("interrupted while waiting for back-pressure — exiting");
                std::process::exit(0);
            }
        }

        // Update status: this process is now indexing the file.
        if let Err(e) = status_ch.start_indexing(&cmd.source_file_path) {
            log::warn!("start_indexing status update failed: {e}");
        }

        // Index the whole crate rooted at working_directory (contains Cargo.toml).
        // index_crate() loads the full HIR via rust-analyzer and covers all source files.
        let storage = if let Some(cached) = storage_cache.get(&cmd.working_directory) {
            log::info!(
                "reusing cached crate index for \"{}\"",
                cmd.working_directory
            );
            cached.clone()
        } else {
            let indexed = parser::index_crate(Path::new(&cmd.working_directory));
            storage_cache.insert(cmd.working_directory.clone(), indexed.clone());
            indexed
        };

        log::info!(
            "indexed \"{}\" — {} nodes, {} edges, {} errors",
            cmd.source_file_path,
            storage.nodes.len(),
            storage.edges.len(),
            storage.errors.len(),
        );

        // Push results.
        if let Err(e) = storage_ch.push(&storage) {
            log::error!("Failed to push storage: {e}");
        }

        // Update status: done with this file.
        if let Err(e) = status_ch.finish_indexing() {
            log::warn!("finish_indexing status update failed: {e}");
        }
    }

    log::info!("process_id={process_id} shutting down");
}
