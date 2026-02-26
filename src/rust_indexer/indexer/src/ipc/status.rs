// StatusChannel: reads/writes IndexingStatus in shared memory.
//
// SHM name: istatus_ipc_<uuid>  (C++: IpcInterprocessIndexingStatusManager)
// Size: 1 MiB

use std::io;

use flatbuffers::FlatBufferBuilder;

use crate::ipc::shm::{is_empty, IpcShm};
use crate::schemas::indexing_status::sourcetrail::ipc::{
    finish_indexing_status_buffer, root_as_indexing_status, IndexingStatus, IndexingStatusArgs,
    ProcessFile, ProcessFileArgs,
};

const SHM_SIZE: usize = 1024 * 1024;

pub struct StatusChannel {
    shm: IpcShm,
    process_id: u64,
}

impl StatusChannel {
    pub fn open(uuid: &str, process_id: u64) -> io::Result<Self> {
        let name = format!("ists_ipc_{uuid}");
        Ok(Self {
            shm: IpcShm::open(&name, SHM_SIZE)?,
            process_id,
        })
    }

    /// Returns true if the app has set the `indexing_interrupted` flag.
    pub fn is_interrupted(&self) -> io::Result<bool> {
        self.shm.read_locked(|data| {
            if is_empty(data) {
                return false;
            }
            root_as_indexing_status(data)
                .map(|s| s.indexing_interrupted())
                .unwrap_or(false)
        })
    }

    /// Record that this process has started indexing `file_path`.
    pub fn start_indexing(&self, file_path: &str) -> io::Result<()> {
        let existing = self.read_existing()?;
        let mut fbb = FlatBufferBuilder::with_capacity(4096);

        // Build all leaf objects first, collecting raw u32 offsets to avoid
        // WIPOffset<T<'_>> borrowing fbb across multiple calls.
        let path_raw = fbb.create_string(file_path).value();
        let pf_raw = {
            let path_off = flatbuffers::WIPOffset::new(path_raw);
            ProcessFile::create(
                &mut fbb,
                &ProcessFileArgs {
                    process_id: self.process_id,
                    file_path: Some(path_off),
                },
            )
            .value()
        };

        let crashed_raws: Vec<u32> = existing
            .crashed_file_paths
            .iter()
            .map(|s| fbb.create_string(s).value())
            .collect();
        let finished_ids: Vec<u64> = existing.finished_process_ids.clone();

        // Reconstruct typed offsets and build vectors.
        let path_off: flatbuffers::WIPOffset<&str> = flatbuffers::WIPOffset::new(path_raw);
        let indexing_v = fbb.create_vector(&[path_off]);
        let indexing_raw = indexing_v.value();

        let pf_off: flatbuffers::WIPOffset<ProcessFile> = flatbuffers::WIPOffset::new(pf_raw);
        let current_v = fbb.create_vector(&[pf_off]);
        let current_raw = current_v.value();

        let crashed_offs: Vec<flatbuffers::WIPOffset<&str>> = crashed_raws
            .iter()
            .map(|&v| flatbuffers::WIPOffset::new(v))
            .collect();
        let crashed_v = if crashed_offs.is_empty() {
            None
        } else {
            Some(fbb.create_vector(&crashed_offs))
        };
        let crashed_raw = crashed_v.map(|v| v.value());

        let finished_v = if finished_ids.is_empty() {
            None
        } else {
            Some(fbb.create_vector(&finished_ids))
        };
        let finished_raw = finished_v.map(|v| v.value());

        let args = IndexingStatusArgs {
            indexing_file_paths: Some(flatbuffers::WIPOffset::new(indexing_raw)),
            current_files: Some(flatbuffers::WIPOffset::new(current_raw)),
            crashed_file_paths: crashed_raw.map(flatbuffers::WIPOffset::new),
            finished_process_ids: finished_raw.map(flatbuffers::WIPOffset::new),
            indexing_interrupted: existing.indexing_interrupted,
            queue_stopped: existing.queue_stopped,
        };
        let status = IndexingStatus::create(&mut fbb, &args);
        finish_indexing_status_buffer(&mut fbb, status);
        self.shm.write_locked(fbb.finished_data())
    }

    /// Record that this process has finished indexing the current file.
    pub fn finish_indexing(&self) -> io::Result<()> {
        let existing = self.read_existing()?;
        let mut fbb = FlatBufferBuilder::with_capacity(4096);

        let mut finished_ids = existing.finished_process_ids.clone();
        finished_ids.push(self.process_id);

        let crashed_raws: Vec<u32> = existing
            .crashed_file_paths
            .iter()
            .map(|s| fbb.create_string(s).value())
            .collect();

        let finished_v = if finished_ids.is_empty() {
            None
        } else {
            Some(fbb.create_vector(&finished_ids))
        };
        let finished_raw = finished_v.map(|v| v.value());

        let crashed_offs: Vec<flatbuffers::WIPOffset<&str>> = crashed_raws
            .iter()
            .map(|&v| flatbuffers::WIPOffset::new(v))
            .collect();
        let crashed_v = if crashed_offs.is_empty() {
            None
        } else {
            Some(fbb.create_vector(&crashed_offs))
        };
        let crashed_raw = crashed_v.map(|v| v.value());

        let args = IndexingStatusArgs {
            indexing_file_paths: None,
            current_files: None,
            crashed_file_paths: crashed_raw.map(flatbuffers::WIPOffset::new),
            finished_process_ids: finished_raw.map(flatbuffers::WIPOffset::new),
            indexing_interrupted: existing.indexing_interrupted,
            queue_stopped: existing.queue_stopped,
        };
        let status = IndexingStatus::create(&mut fbb, &args);
        finish_indexing_status_buffer(&mut fbb, status);
        self.shm.write_locked(fbb.finished_data())
    }

    fn read_existing(&self) -> io::Result<IndexingStatusOwned> {
        self.shm.read_locked(|data| {
            if is_empty(data) {
                return IndexingStatusOwned::default();
            }
            root_as_indexing_status(data)
                .map(IndexingStatusOwned::from_fbs)
                .unwrap_or_default()
        })
    }
}

// ---------------------------------------------------------------------------
// Owned copy of IndexingStatus for read-modify-write
// ---------------------------------------------------------------------------

#[derive(Default)]
struct IndexingStatusOwned {
    crashed_file_paths: Vec<String>,
    finished_process_ids: Vec<u64>,
    indexing_interrupted: bool,
    queue_stopped: bool,
}

impl IndexingStatusOwned {
    fn from_fbs(s: IndexingStatus<'_>) -> Self {
        let str_vec = |v: Option<flatbuffers::Vector<'_, flatbuffers::ForwardsUOffset<&str>>>| {
            v.map(|vec| (0..vec.len()).map(|i| vec.get(i).to_owned()).collect())
                .unwrap_or_default()
        };
        let finished = s
            .finished_process_ids()
            .map(|v| (0..v.len()).map(|i| v.get(i)).collect())
            .unwrap_or_default();
        Self {
            crashed_file_paths: str_vec(s.crashed_file_paths()),
            finished_process_ids: finished,
            indexing_interrupted: s.indexing_interrupted(),
            queue_stopped: s.queue_stopped(),
        }
    }
}
