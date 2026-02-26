// CommandChannel: reads IndexerCommand entries from the shared command queue
// and writes back the shortened queue after each pop.
//
// SHM name: icmd_ipc_<uuid>  (C++: IpcInterprocessIndexerCommandManager)
// Size: 1 MiB (matches C++ constant)

use std::io;

use flatbuffers::FlatBufferBuilder;

use crate::ipc::shm::{is_empty, IpcShm};
use crate::schemas::indexer_command::sourcetrail::ipc::{
    root_as_indexer_command_queue, IndexerCommand, IndexerCommandArgs, IndexerCommandQueue,
    IndexerCommandQueueArgs, IndexerCommandType,
};

const SHM_SIZE: usize = 1024 * 1024; // 1 MiB, matches C++

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
            .read_locked(|data| -> io::Result<Option<OwnedIndexerCommand>> {
                if is_empty(data) {
                    return Ok(None);
                }
                let queue = match root_as_indexer_command_queue(data) {
                    Ok(q) => q,
                    Err(_) => return Ok(None),
                };
                let commands = match queue.commands() {
                    Some(v) => v,
                    None => return Ok(None),
                };
                if commands.is_empty() {
                    return Ok(None);
                }
                // Find the first Rust command.
                let mut rust_idx = None;
                for i in 0..commands.len() {
                    if commands.get(i).type_() == IndexerCommandType::Rust {
                        rust_idx = Some(i);
                        break;
                    }
                }
                let idx = match rust_idx {
                    Some(i) => i,
                    None => return Ok(None),
                };
                let cmd = OwnedIndexerCommand::from_fbs(commands.get(idx));
                Ok(Some(cmd))
            })?
            .and_then(|opt| {
                if opt.is_some() {
                    // Remove the command from the queue and write back.
                    self.remove_first_rust()?;
                }
                Ok(opt)
            })
    }

    fn remove_first_rust(&self) -> io::Result<()> {
        // Read all commands, drop the first Rust one, write back.
        let remaining: Vec<OwnedIndexerCommand> = self.shm.read_locked(|data| {
            if is_empty(data) {
                return Vec::new();
            }
            let queue = match root_as_indexer_command_queue(data) {
                Ok(q) => q,
                Err(_) => return Vec::new(),
            };
            let commands = match queue.commands() {
                Some(v) => v,
                None => return Vec::new(),
            };
            let mut removed = false;
            let mut out = Vec::with_capacity(commands.len());
            for i in 0..commands.len() {
                let c = commands.get(i);
                if !removed && c.type_() == IndexerCommandType::Rust {
                    removed = true;
                    continue;
                }
                out.push(OwnedIndexerCommand::from_fbs(c));
            }
            out
        })?;

        if remaining.is_empty() {
            self.shm.clear_locked()
        } else {
            let buf = serialize_queue(&remaining);
            self.shm.write_locked(&buf)
        }
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
    pub compiler_flags: Vec<String>,
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
            compiler_flags: str_vec(cmd.compiler_flags()),
        }
    }
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
                    compiler_path: None,
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
