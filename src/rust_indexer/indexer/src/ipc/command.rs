// CommandChannel: reads IndexerCommand entries from the shared command queue
// and writes back the shortened queue after each pop.
//
// SHM name: icmd_ipc_<uuid>  (C++: IpcInterprocessIndexerCommandManager)
// Size: 64 MiB (matches C++ constant)

use std::io;

use flatbuffers::FlatBufferBuilder;

use crate::ipc::shm::{IpcShm, is_empty};
use crate::schemas::indexer_command::sourcetrail::ipc::{
    IndexerCommand, IndexerCommandArgs, IndexerCommandQueue, IndexerCommandQueueArgs,
    IndexerCommandType, root_as_indexer_command_queue,
};

const SHM_SIZE: usize = 64 * 1024 * 1024; // 64 MiB, matches C++

pub struct CommandChannel {
    shm: IpcShm,
}

impl CommandChannel {
    pub fn open(uuid: &str) -> io::Result<Self> {
        let name = format!("icmd_ipc_{uuid}");
        Ok(Self {
            shm: IpcShm::open(&name, SHM_SIZE)?,
        })
    }

    /// Pop the first Rust-typed command from the queue.
    /// Returns `None` when the queue is empty or contains no Rust commands.
    pub fn pop_rust_command(&self) -> io::Result<Option<OwnedIndexerCommand>> {
        self.shm
            .read_modify_write_with_result(|data| pop_first_rust_from_queue_bytes(data))
    }

    /// How many commands are currently in the queue.
    pub fn command_count(&self) -> io::Result<usize> {
        self.shm.read_locked(|data| {
            if is_empty(data) {
                return 0;
            }
            root_as_indexer_command_queue(data)
                .ok()
                .and_then(|q| q.commands())
                .map(|v| v.len())
                .unwrap_or(0)
        })
    }
}

// ---------------------------------------------------------------------------
// Owned (heap-allocated) copy of a command
// ---------------------------------------------------------------------------

#[derive(Debug, Clone)]
pub struct OwnedIndexerCommand {
    pub type_: IndexerCommandType,
    pub source_file_path: String,
    pub indexed_paths: Vec<String>,
    pub exclude_filters: Vec<String>,
    pub include_filters: Vec<String>,
    pub working_directory: String,
    /// Cargo features to enable (project model v1).
    pub features: Vec<String>,
    /// Enable all Cargo features (`--all-features`).
    pub all_features: bool,
    /// Do not enable the `default` Cargo feature.
    pub no_default_features: bool,
    /// rustc target triple; empty = host.
    pub target_triple: String,
    pub compiler_flags: Vec<String>,
    pub compiler_path: String,
}

impl OwnedIndexerCommand {
    fn from_fbs(cmd: IndexerCommand<'_>) -> Self {
        let str_vec = |v: Option<flatbuffers::Vector<'_, flatbuffers::ForwardsUOffset<&str>>>| {
            v.map(|vec| (0..vec.len()).map(|i| vec.get(i).to_owned()).collect())
                .unwrap_or_default()
        };
        Self {
            type_: cmd.type_(),
            source_file_path: cmd.source_file_path().unwrap_or("").to_owned(),
            indexed_paths: str_vec(cmd.indexed_paths()),
            exclude_filters: str_vec(cmd.exclude_filters()),
            include_filters: str_vec(cmd.include_filters()),
            working_directory: cmd.working_directory().unwrap_or("").to_owned(),
            features: str_vec(cmd.features()),
            all_features: cmd.all_features(),
            no_default_features: cmd.no_default_features(),
            target_triple: cmd.target_triple().unwrap_or("").to_owned(),
            compiler_flags: str_vec(cmd.compiler_flags()),
            compiler_path: cmd.compiler_path().unwrap_or("").to_owned(),
        }
    }
}

fn pop_first_rust_from_queue_bytes(
    data: &[u8],
) -> io::Result<(Option<Vec<u8>>, Option<OwnedIndexerCommand>)> {
    if is_empty(data) {
        return Ok((None, None));
    }

    let queue = match root_as_indexer_command_queue(data) {
        Ok(q) => q,
        Err(_) => return Ok((None, None)),
    };
    let commands = match queue.commands() {
        Some(v) => v,
        None => return Ok((None, None)),
    };
    if commands.is_empty() {
        return Ok((None, None));
    }

    let mut rust_idx = None;
    for i in 0..commands.len() {
        if commands.get(i).type_() == IndexerCommandType::Rust {
            rust_idx = Some(i);
            break;
        }
    }
    let idx = match rust_idx {
        Some(i) => i,
        None => return Ok((None, None)),
    };

    let popped = OwnedIndexerCommand::from_fbs(commands.get(idx));

    let mut remaining = Vec::with_capacity(commands.len().saturating_sub(1));
    for i in 0..commands.len() {
        if i == idx {
            continue;
        }
        remaining.push(OwnedIndexerCommand::from_fbs(commands.get(i)));
    }

    let rewritten = if remaining.is_empty() {
        vec![0u8; 4]
    } else {
        serialize_queue(&remaining)
    };
    Ok((Some(rewritten), Some(popped)))
}

// ---------------------------------------------------------------------------
// Serialization helper
// ---------------------------------------------------------------------------

fn serialize_queue(commands: &[OwnedIndexerCommand]) -> Vec<u8> {
    let mut fbb = FlatBufferBuilder::with_capacity(4096);

    let cmd_offsets: Vec<_> = commands
        .iter()
        .map(|cmd| {
            let src = fbb.create_string(&cmd.source_file_path);
            let wdir = fbb.create_string(&cmd.working_directory);
            let indexed: Vec<_> = cmd
                .indexed_paths
                .iter()
                .map(|s| fbb.create_string(s))
                .collect();
            let indexed_v = fbb.create_vector(&indexed);
            let exclude: Vec<_> = cmd
                .exclude_filters
                .iter()
                .map(|s| fbb.create_string(s))
                .collect();
            let exclude_v = fbb.create_vector(&exclude);
            let include: Vec<_> = cmd
                .include_filters
                .iter()
                .map(|s| fbb.create_string(s))
                .collect();
            let include_v = fbb.create_vector(&include);
            let flags: Vec<_> = cmd
                .compiler_flags
                .iter()
                .map(|s| fbb.create_string(s))
                .collect();
            let flags_v = fbb.create_vector(&flags);
            let compiler_path = if cmd.compiler_path.is_empty() {
                None
            } else {
                Some(fbb.create_string(&cmd.compiler_path))
            };
            let features: Vec<_> = cmd.features.iter().map(|s| fbb.create_string(s)).collect();
            let features_v = fbb.create_vector(&features);
            let target_triple = if cmd.target_triple.is_empty() {
                None
            } else {
                Some(fbb.create_string(&cmd.target_triple))
            };
            IndexerCommand::create(
                &mut fbb,
                &IndexerCommandArgs {
                    type_: cmd.type_,
                    source_file_path: Some(src),
                    indexed_paths: Some(indexed_v),
                    exclude_filters: Some(exclude_v),
                    include_filters: Some(include_v),
                    working_directory: Some(wdir),
                    compiler_flags: Some(flags_v),
                    compiler_path,
                    features: Some(features_v),
                    all_features: cmd.all_features,
                    no_default_features: cmd.no_default_features,
                    target_triple,
                },
            )
        })
        .collect();

    let cmds_v = fbb.create_vector(&cmd_offsets);
    let queue = IndexerCommandQueue::create(
        &mut fbb,
        &IndexerCommandQueueArgs {
            commands: Some(cmds_v),
        },
    );
    fbb.finish(queue, None);
    fbb.finished_data().to_vec()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn cmd(kind: IndexerCommandType, src: &str, compiler_path: &str) -> OwnedIndexerCommand {
        OwnedIndexerCommand {
            type_: kind,
            source_file_path: src.to_owned(),
            indexed_paths: vec![src.to_owned()],
            exclude_filters: Vec::new(),
            include_filters: Vec::new(),
            working_directory: "/tmp/project".to_owned(),
            compiler_flags: vec!["-std=c++20".to_owned()],
            compiler_path: compiler_path.to_owned(),
            features: Vec::new(),
            all_features: false,
            no_default_features: false,
            target_triple: String::new(),
        }
    }

    #[test]
    fn pop_first_rust_preserves_compiler_path_on_remaining_cxx_commands() {
        let input = vec![
            cmd(IndexerCommandType::Cxx, "a.cpp", "/usr/bin/clang++"),
            cmd(IndexerCommandType::Rust, "crate", ""),
            cmd(IndexerCommandType::Cxx, "b.cpp", "/opt/clang/bin/clang++"),
        ];
        let bytes = serialize_queue(&input);

        let (rewritten, popped) = pop_first_rust_from_queue_bytes(&bytes).unwrap();
        let popped = popped.unwrap();
        assert_eq!(popped.type_, IndexerCommandType::Rust);

        let rewritten = rewritten.unwrap();
        let queue = root_as_indexer_command_queue(&rewritten).unwrap();
        let commands = queue.commands().unwrap();
        assert_eq!(commands.len(), 2);
        assert_eq!(commands.get(0).type_(), IndexerCommandType::Cxx);
        assert_eq!(commands.get(0).compiler_path().unwrap(), "/usr/bin/clang++");
        assert_eq!(commands.get(1).type_(), IndexerCommandType::Cxx);
        assert_eq!(
            commands.get(1).compiler_path().unwrap(),
            "/opt/clang/bin/clang++"
        );
    }

    #[test]
    fn pop_first_rust_returns_none_and_does_not_rewrite_when_no_rust_command_exists() {
        let input = vec![cmd(IndexerCommandType::Cxx, "only.cpp", "/usr/bin/clang++")];
        let bytes = serialize_queue(&input);

        let (rewritten, popped) = pop_first_rust_from_queue_bytes(&bytes).unwrap();
        assert!(rewritten.is_none());
        assert!(popped.is_none());
    }
}
