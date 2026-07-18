// StatusChannel: reads/writes IndexingStatus in shared memory.
//
// SHM name: istatus_ipc_<uuid>  (C++: IpcInterprocessIndexingStatusManager)
// Size: 1 MiB

use std::io;

use flatbuffers::FlatBufferBuilder;

use crate::ipc::shm::{IpcShm, is_empty};
use crate::schemas::indexing_status::sourcetrail::ipc::{
    IndexingStatus, IndexingStatusArgs, ProcessFile, ProcessFileArgs,
    finish_indexing_status_buffer, root_as_indexing_status,
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
    ///
    /// A zeroed/uninitialized segment is a legitimate "no status yet" (empty →
    /// not interrupted). A NON-empty buffer that fails flatbuffers verification is
    /// surfaced as an error rather than silently read as "not interrupted" — the
    /// caller logs it (main.rs) instead of missing a genuine app→indexer signal
    /// (see docs/adr/ADR-0003).
    pub fn is_interrupted(&self) -> io::Result<bool> {
        self.shm.read_locked(|data| -> io::Result<bool> {
            if is_empty(data) {
                return Ok(false);
            }
            root_as_indexing_status(data)
                .map(|s| s.indexing_interrupted())
                .map_err(|e| {
                    io::Error::new(
                        io::ErrorKind::InvalidData,
                        format!("IndexingStatus failed flatbuffers verification: {e}"),
                    )
                })
        })?
    }

    /// Record that this process has started indexing `file_path`.
    pub fn start_indexing(&self, file_path: &str) -> io::Result<()> {
        self.shm.read_modify_write_with_result(|data| {
            let mut existing = if is_empty(data) {
                IndexingStatusOwned::default()
            } else {
                root_as_indexing_status(data)
                    .map(IndexingStatusOwned::from_fbs)
                    .unwrap_or_default()
            };

            existing.apply_start_indexing(self.process_id, file_path);
            Ok((Some(existing.to_bytes()), ()))
        })
    }

    /// Per-file progress update within one command: replaces this process's
    /// current file WITHOUT the crash bookkeeping of `start_indexing`.
    ///
    /// `current_files` means "will be reported as a crashed translation unit
    /// if this process never finishes" (TaskBuildIndex::doExit); the GUI
    /// progress display reads the `indexing_file_paths` queue, which this
    /// still feeds. Calling start_indexing per file left one
    /// started-but-unfinished entry per command, which the app reported as a
    /// phantom crashed translation unit.
    pub fn update_indexing(&self, file_path: &str) -> io::Result<()> {
        self.shm.read_modify_write_with_result(|data| {
            let mut existing = if is_empty(data) {
                IndexingStatusOwned::default()
            } else {
                root_as_indexing_status(data)
                    .map(IndexingStatusOwned::from_fbs)
                    .unwrap_or_default()
            };

            existing.apply_update_indexing(self.process_id, file_path);
            Ok((Some(existing.to_bytes()), ()))
        })
    }

    /// Record that this process has finished indexing the current file.
    pub fn finish_indexing(&self) -> io::Result<()> {
        self.shm.read_modify_write_with_result(|data| {
            let mut existing = if is_empty(data) {
                IndexingStatusOwned::default()
            } else {
                root_as_indexing_status(data)
                    .map(IndexingStatusOwned::from_fbs)
                    .unwrap_or_default()
            };

            existing.apply_finish_indexing(self.process_id);
            Ok((Some(existing.to_bytes()), ()))
        })
    }
}

// ---------------------------------------------------------------------------
// Owned copy of IndexingStatus for read-modify-write
// ---------------------------------------------------------------------------

#[derive(Default)]
struct IndexingStatusOwned {
    indexing_file_paths: Vec<String>,
    current_files: Vec<(u64, String)>,
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
        let current_files = s
            .current_files()
            .map(|v| {
                (0..v.len())
                    .map(|i| {
                        let pf = v.get(i);
                        (pf.process_id(), pf.file_path().unwrap_or("").to_owned())
                    })
                    .collect()
            })
            .unwrap_or_default();
        Self {
            indexing_file_paths: str_vec(s.indexing_file_paths()),
            current_files,
            crashed_file_paths: str_vec(s.crashed_file_paths()),
            finished_process_ids: finished,
            indexing_interrupted: s.indexing_interrupted(),
            queue_stopped: s.queue_stopped(),
        }
    }

    fn to_bytes(&self) -> Vec<u8> {
        let mut fbb = FlatBufferBuilder::with_capacity(4096);

        let indexing_paths: Vec<flatbuffers::WIPOffset<&str>> = self
            .indexing_file_paths
            .iter()
            .map(|s| fbb.create_string(s))
            .collect();
        let indexing_v = if indexing_paths.is_empty() {
            None
        } else {
            Some(fbb.create_vector(&indexing_paths))
        };

        let current_files: Vec<flatbuffers::WIPOffset<ProcessFile>> = self
            .current_files
            .iter()
            .map(|(pid, path)| {
                let path = fbb.create_string(path);
                ProcessFile::create(
                    &mut fbb,
                    &ProcessFileArgs {
                        process_id: *pid,
                        file_path: Some(path),
                    },
                )
            })
            .collect();
        let current_v = if current_files.is_empty() {
            None
        } else {
            Some(fbb.create_vector(&current_files))
        };

        let crashed_paths: Vec<flatbuffers::WIPOffset<&str>> = self
            .crashed_file_paths
            .iter()
            .map(|s| fbb.create_string(s))
            .collect();
        let crashed_v = if crashed_paths.is_empty() {
            None
        } else {
            Some(fbb.create_vector(&crashed_paths))
        };

        let finished_v = if self.finished_process_ids.is_empty() {
            None
        } else {
            Some(fbb.create_vector(&self.finished_process_ids))
        };

        let status = IndexingStatus::create(
            &mut fbb,
            &IndexingStatusArgs {
                indexing_file_paths: indexing_v,
                current_files: current_v,
                crashed_file_paths: crashed_v,
                finished_process_ids: finished_v,
                indexing_interrupted: self.indexing_interrupted,
                queue_stopped: self.queue_stopped,
            },
        );
        finish_indexing_status_buffer(&mut fbb, status);
        fbb.finished_data().to_vec()
    }

    fn apply_start_indexing(&mut self, process_id: u64, file_path: &str) {
        self.indexing_file_paths.push(file_path.to_owned());

        if let Some(idx) = self
            .current_files
            .iter()
            .position(|(pid, _)| *pid == process_id)
        {
            let (_, previous_file_path) = self.current_files.remove(idx);
            if !self
                .crashed_file_paths
                .iter()
                .any(|p| p == &previous_file_path)
            {
                self.crashed_file_paths.push(previous_file_path);
            }
        }

        self.current_files.push((process_id, file_path.to_owned()));
    }

    /// Progress update: feed the GUI queue and replace the current file
    /// without marking the previous one as crashed — it did not crash, it is
    /// part of the same still-running command.
    fn apply_update_indexing(&mut self, process_id: u64, file_path: &str) {
        self.indexing_file_paths.push(file_path.to_owned());
        self.current_files.retain(|(pid, _)| *pid != process_id);
        self.current_files.push((process_id, file_path.to_owned()));
    }

    fn apply_finish_indexing(&mut self, process_id: u64) {
        let mut finished_file_path = String::new();
        if let Some(idx) = self
            .current_files
            .iter()
            .position(|(pid, _)| *pid == process_id)
        {
            let (_, path) = self.current_files.remove(idx);
            finished_file_path = path;
        }

        self.finished_process_ids.push(process_id);

        if finished_file_path.is_empty() {
            return;
        }

        self.crashed_file_paths.retain(|p| p != &finished_file_path);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn start_indexing_preserves_other_process_state() {
        let mut status = IndexingStatusOwned {
            indexing_file_paths: vec!["cxx_a.cpp".to_owned()],
            current_files: vec![(1, "cxx_a.cpp".to_owned())],
            crashed_file_paths: vec![],
            finished_process_ids: vec![],
            indexing_interrupted: false,
            queue_stopped: false,
        };

        status.apply_start_indexing(7, "crate_root");

        assert_eq!(
            status.indexing_file_paths,
            vec!["cxx_a.cpp".to_owned(), "crate_root".to_owned()]
        );
        assert!(
            status
                .current_files
                .iter()
                .any(|(pid, path)| *pid == 1 && path == "cxx_a.cpp")
        );
        assert!(
            status
                .current_files
                .iter()
                .any(|(pid, path)| *pid == 7 && path == "crate_root")
        );
    }

    #[test]
    fn start_indexing_marks_previous_file_as_crashed_for_same_process() {
        let mut status = IndexingStatusOwned {
            current_files: vec![(7, "old.rs".to_owned())],
            ..Default::default()
        };

        status.apply_start_indexing(7, "new.rs");

        assert!(status.crashed_file_paths.iter().any(|p| p == "old.rs"));
        assert_eq!(
            status
                .current_files
                .iter()
                .filter(|(pid, _)| *pid == 7)
                .count(),
            1
        );
        assert!(
            status
                .current_files
                .iter()
                .any(|(pid, path)| *pid == 7 && path == "new.rs")
        );
    }

    #[test]
    fn update_indexing_replaces_current_file_without_crash_mark() {
        let mut status = IndexingStatusOwned {
            current_files: vec![(1, "cxx_a.cpp".to_owned()), (7, "old.rs".to_owned())],
            ..Default::default()
        };

        status.apply_update_indexing(7, "new.rs");

        assert!(status.crashed_file_paths.is_empty());
        assert_eq!(status.indexing_file_paths, vec!["new.rs".to_owned()]);
        assert!(
            status
                .current_files
                .iter()
                .any(|(pid, path)| *pid == 1 && path == "cxx_a.cpp")
        );
        assert_eq!(
            status
                .current_files
                .iter()
                .filter(|(pid, _)| *pid == 7)
                .map(|(_, p)| p.as_str())
                .collect::<Vec<_>>(),
            vec!["new.rs"]
        );
    }

    #[test]
    fn finish_indexing_clears_current_file_and_unmarks_crash() {
        let mut status = IndexingStatusOwned {
            current_files: vec![(7, "crate/src/lib.rs".to_owned())],
            crashed_file_paths: vec!["crate/src/lib.rs".to_owned(), "other.rs".to_owned()],
            ..Default::default()
        };

        status.apply_finish_indexing(7);

        assert!(!status.current_files.iter().any(|(pid, _)| *pid == 7));
        assert!(status.finished_process_ids.iter().any(|pid| *pid == 7));
        assert!(
            !status
                .crashed_file_paths
                .iter()
                .any(|p| p == "crate/src/lib.rs")
        );
        assert!(status.crashed_file_paths.iter().any(|p| p == "other.rs"));
    }
}
