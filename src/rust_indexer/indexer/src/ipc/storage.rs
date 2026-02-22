// StorageChannel: pushes IntermediateStorage results back to the app.
//
// SHM name: iist_ipc_<processId>_<uuid>  (C++: IpcInterprocessIntermediateStorageManager)
// Wire format: [u32 count][u32 size0][FlatBuffers bytes0][u32 size1][FlatBuffers bytes1]...
// This matches the C++ implementation exactly.
// Size: 3 MiB (matches C++ constant of 3 * 1048576)

use std::io;

use crate::ipc::shm::IpcShm;
use crate::schemas::intermediate_storage::sourcetrail::ipc::{
    IntermediateStorage, IntermediateStorageArgs, StorageComponentAccess,
    StorageComponentAccessArgs, StorageEdge, StorageEdgeArgs, StorageError, StorageErrorArgs,
    StorageFile, StorageFileArgs, StorageLocalSymbol, StorageLocalSymbolArgs, StorageNode,
    StorageNodeArgs, StorageOccurrence, StorageOccurrenceArgs, StorageSourceLocation,
    StorageSourceLocationArgs, StorageSymbol, StorageSymbolArgs,
};
use flatbuffers::FlatBufferBuilder;

const SHM_SIZE: usize = 3 * 1024 * 1024; // 3 MiB, matches C++

pub struct StorageChannel {
    shm: IpcShm,
}

impl StorageChannel {
    pub fn open(uuid: &str, process_id: u64) -> io::Result<Self> {
        let name = format!("iist_ipc_{process_id}_{uuid}");
        Ok(Self {
            shm: IpcShm::open(&name, SHM_SIZE)?,
        })
    }

    /// How many IntermediateStorage entries are currently queued.
    /// The C++ app uses this for back-pressure (waits when count >= 2).
    pub fn storage_count(&self) -> io::Result<usize> {
        self.shm.read_locked(|data| {
            if data.len() < 4 {
                return 0;
            }
            let count = u32::from_ne_bytes([data[0], data[1], data[2], data[3]]);
            count as usize
        })
    }

    /// Append one `IntermediateStorage` to the queue in shared memory.
    /// Wire format: [u32 count][u32 size0][bytes0]...
    pub fn push(&self, storage: &OwnedIntermediateStorage) -> io::Result<()> {
        let entry_bytes = serialize_one_storage(storage);
        let entry_size = entry_bytes.len() as u32;

        self.shm.read_modify_write(|data| {
            // Read current count and existing payload.
            let count = if data.len() >= 4 {
                u32::from_ne_bytes([data[0], data[1], data[2], data[3]])
            } else {
                0
            };

            // Calculate existing payload size by walking entries.
            let mut existing_payload_end = 4usize; // after count header
            let mut remaining = count;
            while remaining > 0 && existing_payload_end + 4 <= data.len() {
                let sz = u32::from_ne_bytes([
                    data[existing_payload_end],
                    data[existing_payload_end + 1],
                    data[existing_payload_end + 2],
                    data[existing_payload_end + 3],
                ]) as usize;
                existing_payload_end += 4 + sz;
                remaining -= 1;
            }

            // Build new buffer.
            let new_count = count + 1;
            let existing_payload_len = existing_payload_end - 4;
            let total = 4 + existing_payload_len + 4 + entry_bytes.len();

            let mut buf = Vec::with_capacity(total);
            buf.extend_from_slice(&new_count.to_ne_bytes());
            if existing_payload_len > 0 {
                buf.extend_from_slice(&data[4..existing_payload_end]);
            }
            buf.extend_from_slice(&entry_size.to_ne_bytes());
            buf.extend_from_slice(&entry_bytes);
            buf
        })
    }
}

// ---------------------------------------------------------------------------
// Owned intermediate storage (heap copy for read-modify-write)
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Default)]
pub struct OwnedIntermediateStorage {
    pub next_id: i64,
    pub nodes: Vec<OwnedStorageNode>,
    pub files: Vec<OwnedStorageFile>,
    pub edges: Vec<OwnedStorageEdge>,
    pub symbols: Vec<OwnedStorageSymbol>,
    pub source_locations: Vec<OwnedStorageSourceLocation>,
    pub local_symbols: Vec<OwnedStorageLocalSymbol>,
    pub occurrences: Vec<OwnedStorageOccurrence>,
    pub component_accesses: Vec<OwnedStorageComponentAccess>,
    pub errors: Vec<OwnedStorageError>,
}

#[derive(Debug, Clone)]
pub struct OwnedStorageNode {
    pub id: i64,
    pub type_: i32,
    pub serialized_name: String,
}

#[derive(Debug, Clone)]
pub struct OwnedStorageFile {
    pub id: i64,
    pub file_path: String,
    pub language_identifier: String,
    pub indexed: bool,
    pub complete: bool,
}

#[derive(Debug, Clone)]
pub struct OwnedStorageEdge {
    pub id: i64,
    pub type_: i32,
    pub source_node_id: i64,
    pub target_node_id: i64,
}

#[derive(Debug, Clone)]
pub struct OwnedStorageSymbol {
    pub id: i64,
    pub definition_kind: i32,
}

#[derive(Debug, Clone)]
pub struct OwnedStorageSourceLocation {
    pub id: i64,
    pub file_node_id: i64,
    pub start_line: u32,
    pub start_col: u32,
    pub end_line: u32,
    pub end_col: u32,
    pub type_: i32,
}

#[derive(Debug, Clone)]
pub struct OwnedStorageLocalSymbol {
    pub id: i64,
    pub name: String,
}

#[derive(Debug, Clone)]
pub struct OwnedStorageOccurrence {
    pub element_id: i64,
    pub source_location_id: i64,
}

#[derive(Debug, Clone)]
pub struct OwnedStorageComponentAccess {
    pub node_id: i64,
    pub type_: i32,
}

#[derive(Debug, Clone)]
pub struct OwnedStorageError {
    pub id: i64,
    pub message: String,
    pub translation_unit: String,
    pub fatal: bool,
    pub indexed: bool,
}

impl OwnedIntermediateStorage {
    #[cfg(test)]
    pub(crate) fn from_fbs(s: IntermediateStorage<'_>) -> Self {
        let nodes = s
            .nodes()
            .map(|v| {
                (0..v.len())
                    .map(|i| {
                        let n = v.get(i);
                        OwnedStorageNode {
                            id: n.id(),
                            type_: n.type_(),
                            serialized_name: n.serialized_name().unwrap_or("").to_owned(),
                        }
                    })
                    .collect()
            })
            .unwrap_or_default();

        let files = s
            .files()
            .map(|v| {
                (0..v.len())
                    .map(|i| {
                        let f = v.get(i);
                        OwnedStorageFile {
                            id: f.id(),
                            file_path: f.file_path().unwrap_or("").to_owned(),
                            language_identifier: f.language_identifier().unwrap_or("").to_owned(),
                            indexed: f.indexed(),
                            complete: f.complete(),
                        }
                    })
                    .collect()
            })
            .unwrap_or_default();

        let edges = s
            .edges()
            .map(|v| {
                (0..v.len())
                    .map(|i| {
                        let e = v.get(i);
                        OwnedStorageEdge {
                            id: e.id(),
                            type_: e.type_(),
                            source_node_id: e.source_node_id(),
                            target_node_id: e.target_node_id(),
                        }
                    })
                    .collect()
            })
            .unwrap_or_default();

        let symbols = s
            .symbols()
            .map(|v| {
                (0..v.len())
                    .map(|i| {
                        let sym = v.get(i);
                        OwnedStorageSymbol {
                            id: sym.id(),
                            definition_kind: sym.definition_kind(),
                        }
                    })
                    .collect()
            })
            .unwrap_or_default();

        let source_locations = s
            .source_locations()
            .map(|v| {
                (0..v.len())
                    .map(|i| {
                        let loc = v.get(i);
                        OwnedStorageSourceLocation {
                            id: loc.id(),
                            file_node_id: loc.file_node_id(),
                            start_line: loc.start_line(),
                            start_col: loc.start_col(),
                            end_line: loc.end_line(),
                            end_col: loc.end_col(),
                            type_: loc.type_(),
                        }
                    })
                    .collect()
            })
            .unwrap_or_default();

        let local_symbols = s
            .local_symbols()
            .map(|v| {
                (0..v.len())
                    .map(|i| {
                        let ls = v.get(i);
                        OwnedStorageLocalSymbol {
                            id: ls.id(),
                            name: ls.name().unwrap_or("").to_owned(),
                        }
                    })
                    .collect()
            })
            .unwrap_or_default();

        let occurrences = s
            .occurrences()
            .map(|v| {
                (0..v.len())
                    .map(|i| {
                        let o = v.get(i);
                        OwnedStorageOccurrence {
                            element_id: o.element_id(),
                            source_location_id: o.source_location_id(),
                        }
                    })
                    .collect()
            })
            .unwrap_or_default();

        let component_accesses = s
            .component_accesses()
            .map(|v| {
                (0..v.len())
                    .map(|i| {
                        let ca = v.get(i);
                        OwnedStorageComponentAccess {
                            node_id: ca.node_id(),
                            type_: ca.type_(),
                        }
                    })
                    .collect()
            })
            .unwrap_or_default();

        let errors = s
            .errors()
            .map(|v| {
                (0..v.len())
                    .map(|i| {
                        let e = v.get(i);
                        OwnedStorageError {
                            id: e.id(),
                            message: e.message().unwrap_or("").to_owned(),
                            translation_unit: e.translation_unit().unwrap_or("").to_owned(),
                            fatal: e.fatal(),
                            indexed: e.indexed(),
                        }
                    })
                    .collect()
            })
            .unwrap_or_default();

        Self {
            next_id: s.next_id(),
            nodes,
            files,
            edges,
            symbols,
            source_locations,
            local_symbols,
            occurrences,
            component_accesses,
            errors,
        }
    }
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

fn serialize_one_storage(s: &OwnedIntermediateStorage) -> Vec<u8> {
    let mut fbb = FlatBufferBuilder::with_capacity(16 * 1024);
    let raw = build_storage_inline(&mut fbb, s).value();
    let offset: flatbuffers::WIPOffset<IntermediateStorage> = flatbuffers::WIPOffset::new(raw);
    fbb.finish(offset, None);
    fbb.finished_data().to_vec()
}

fn build_storage_inline<'bldr>(
    fbb: &'bldr mut FlatBufferBuilder,
    s: &OwnedIntermediateStorage,
) -> flatbuffers::WIPOffset<IntermediateStorage<'bldr>> {
    let mut node_offsets: Vec<flatbuffers::WIPOffset<StorageNode>> =
        Vec::with_capacity(s.nodes.len());
    for n in &s.nodes {
        let name = fbb.create_string(&n.serialized_name);
        node_offsets.push(StorageNode::create(
            fbb,
            &StorageNodeArgs {
                id: n.id,
                type_: n.type_,
                serialized_name: Some(name),
            },
        ));
    }
    let nodes_v = fbb.create_vector(&node_offsets);

    let mut file_offsets: Vec<flatbuffers::WIPOffset<StorageFile>> =
        Vec::with_capacity(s.files.len());
    for f in &s.files {
        let path = fbb.create_string(&f.file_path);
        let lang = fbb.create_string(&f.language_identifier);
        file_offsets.push(StorageFile::create(
            fbb,
            &StorageFileArgs {
                id: f.id,
                file_path: Some(path),
                language_identifier: Some(lang),
                indexed: f.indexed,
                complete: f.complete,
            },
        ));
    }
    let files_v = fbb.create_vector(&file_offsets);

    let mut edge_offsets: Vec<flatbuffers::WIPOffset<StorageEdge>> =
        Vec::with_capacity(s.edges.len());
    for e in &s.edges {
        edge_offsets.push(StorageEdge::create(
            fbb,
            &StorageEdgeArgs {
                id: e.id,
                type_: e.type_,
                source_node_id: e.source_node_id,
                target_node_id: e.target_node_id,
            },
        ));
    }
    let edges_v = fbb.create_vector(&edge_offsets);

    let mut sym_offsets: Vec<flatbuffers::WIPOffset<StorageSymbol>> =
        Vec::with_capacity(s.symbols.len());
    for sym in &s.symbols {
        sym_offsets.push(StorageSymbol::create(
            fbb,
            &StorageSymbolArgs {
                id: sym.id,
                definition_kind: sym.definition_kind,
            },
        ));
    }
    let symbols_v = fbb.create_vector(&sym_offsets);

    let mut loc_offsets: Vec<flatbuffers::WIPOffset<StorageSourceLocation>> =
        Vec::with_capacity(s.source_locations.len());
    for loc in &s.source_locations {
        loc_offsets.push(StorageSourceLocation::create(
            fbb,
            &StorageSourceLocationArgs {
                id: loc.id,
                file_node_id: loc.file_node_id,
                start_line: loc.start_line,
                start_col: loc.start_col,
                end_line: loc.end_line,
                end_col: loc.end_col,
                type_: loc.type_,
            },
        ));
    }
    let locs_v = fbb.create_vector(&loc_offsets);

    let mut ls_offsets: Vec<flatbuffers::WIPOffset<StorageLocalSymbol>> =
        Vec::with_capacity(s.local_symbols.len());
    for ls in &s.local_symbols {
        let name = fbb.create_string(&ls.name);
        ls_offsets.push(StorageLocalSymbol::create(
            fbb,
            &StorageLocalSymbolArgs {
                id: ls.id,
                name: Some(name),
            },
        ));
    }
    let local_syms_v = fbb.create_vector(&ls_offsets);

    let mut occ_offsets: Vec<flatbuffers::WIPOffset<StorageOccurrence>> =
        Vec::with_capacity(s.occurrences.len());
    for o in &s.occurrences {
        occ_offsets.push(StorageOccurrence::create(
            fbb,
            &StorageOccurrenceArgs {
                element_id: o.element_id,
                source_location_id: o.source_location_id,
            },
        ));
    }
    let occs_v = fbb.create_vector(&occ_offsets);

    let mut ca_offsets: Vec<flatbuffers::WIPOffset<StorageComponentAccess>> =
        Vec::with_capacity(s.component_accesses.len());
    for ca in &s.component_accesses {
        ca_offsets.push(StorageComponentAccess::create(
            fbb,
            &StorageComponentAccessArgs {
                node_id: ca.node_id,
                type_: ca.type_,
            },
        ));
    }
    let cas_v = fbb.create_vector(&ca_offsets);

    let mut err_offsets: Vec<flatbuffers::WIPOffset<StorageError>> =
        Vec::with_capacity(s.errors.len());
    for e in &s.errors {
        let msg = fbb.create_string(&e.message);
        let tu = fbb.create_string(&e.translation_unit);
        err_offsets.push(StorageError::create(
            fbb,
            &StorageErrorArgs {
                id: e.id,
                message: Some(msg),
                translation_unit: Some(tu),
                fatal: e.fatal,
                indexed: e.indexed,
            },
        ));
    }
    let errs_v = fbb.create_vector(&err_offsets);

    IntermediateStorage::create(
        fbb,
        &IntermediateStorageArgs {
            next_id: s.next_id,
            nodes: Some(nodes_v),
            files: Some(files_v),
            edges: Some(edges_v),
            symbols: Some(symbols_v),
            source_locations: Some(locs_v),
            local_symbols: Some(local_syms_v),
            occurrences: Some(occs_v),
            component_accesses: Some(cas_v),
            errors: Some(errs_v),
        },
    )
}
