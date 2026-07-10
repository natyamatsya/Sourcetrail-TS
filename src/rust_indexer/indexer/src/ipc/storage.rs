// StorageChannel: pushes IntermediateStorage results back to the app.
//
// SHM name: iist_ipc_<processId>_<uuid>  (C++: IpcInterprocessIntermediateStorageManager)
// Wire format:
//   [u64 needed_capacity][u32 count][u32 size0][FlatBuffers bytes0]...
// Header layout and queue encoding mirror the current C++ implementation.
// Size: 16 MiB (matches C++ owner segment size)

use std::io;

use crate::ipc::shm::IpcShm;
use crate::schemas::intermediate_storage::sourcetrail::ipc::{
    IntermediateStorage, IntermediateStorageArgs, IntermediateStorageQueue,
    IntermediateStorageQueueArgs, StorageComponentAccess, StorageComponentAccessArgs, StorageEdge,
    StorageEdgeArgs, StorageError, StorageErrorArgs, StorageFile, StorageFileArgs,
    StorageLocalSymbol, StorageLocalSymbolArgs, StorageNode, StorageNodeArgs, StorageOccurrence,
    StorageOccurrenceArgs, StorageSourceLocation, StorageSourceLocationArgs, StorageSymbol,
    StorageSymbolArgs,
};
use flatbuffers::FlatBufferBuilder;

const SHM_SIZE: usize = 16 * 1024 * 1024; // 16 MiB, matches C++
const CAP_FIELD_SIZE: usize = std::mem::size_of::<u64>();
const COUNT_FIELD_SIZE: usize = std::mem::size_of::<u32>();
const HEADER_SIZE: usize = CAP_FIELD_SIZE + COUNT_FIELD_SIZE;

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
        self.shm.read_locked(|data| read_count(data) as usize)
    }

    /// Append one `IntermediateStorage` to the queue in shared memory.
    /// Wire format: [u64 needed_capacity][u32 count][u32 size0][bytes0]...
    pub fn push(&self, storage: &OwnedIntermediateStorage) -> io::Result<()> {
        let entry_bytes = serialize_one_storage(storage);
        loop {
            let outcome = self.shm.read_modify_write_with_result(|data| {
                let required_size = required_queue_size_for_append(data, entry_bytes.len())?;
                if required_size > data.len() {
                    let grow_to = growth_target(required_size);
                    let signaled = with_needed_capacity_signal(data, grow_to)?;
                    return Ok((Some(signaled), PushOutcome::NeedsGrow(grow_to)));
                }

                let rewritten = append_storage_entry(data, &entry_bytes)?;
                Ok((Some(rewritten), PushOutcome::Written))
            })?;

            match outcome {
                PushOutcome::Written => return Ok(()),
                PushOutcome::NeedsGrow(grow_to) => self.shm.grow(grow_to)?,
            }
        }
    }
}

enum PushOutcome {
    Written,
    NeedsGrow(usize),
}

fn read_u64_at(data: &[u8], offset: usize) -> Option<u64> {
    let bytes: [u8; 8] = data.get(offset..offset + CAP_FIELD_SIZE)?.try_into().ok()?;
    Some(u64::from_ne_bytes(bytes))
}

fn read_u32_at(data: &[u8], offset: usize) -> Option<u32> {
    let bytes: [u8; 4] = data
        .get(offset..offset + COUNT_FIELD_SIZE)?
        .try_into()
        .ok()?;
    Some(u32::from_ne_bytes(bytes))
}

fn write_u64_at(data: &mut [u8], offset: usize, value: u64) {
    data[offset..offset + CAP_FIELD_SIZE].copy_from_slice(&value.to_ne_bytes());
}

fn write_u32_at(data: &mut [u8], offset: usize, value: u32) {
    data[offset..offset + COUNT_FIELD_SIZE].copy_from_slice(&value.to_ne_bytes());
}

fn read_count(data: &[u8]) -> u32 {
    if data.len() < HEADER_SIZE {
        return 0;
    }
    read_u32_at(data, CAP_FIELD_SIZE).unwrap_or(0)
}

fn growth_target(required_size: usize) -> usize {
    required_size.checked_mul(2).unwrap_or(required_size)
}

fn with_needed_capacity_signal(queue_bytes: &[u8], needed_capacity: usize) -> io::Result<Vec<u8>> {
    let needed_capacity = u64::try_from(needed_capacity).map_err(|_| {
        io::Error::new(
            io::ErrorKind::InvalidInput,
            "needed capacity does not fit into u64 signal field",
        )
    })?;

    let mut signaled = if queue_bytes.len() >= HEADER_SIZE {
        queue_bytes.to_vec()
    } else {
        vec![0u8; HEADER_SIZE]
    };

    if signaled.len() < HEADER_SIZE {
        signaled.resize(HEADER_SIZE, 0);
    }

    if queue_bytes.len() < HEADER_SIZE {
        write_u32_at(&mut signaled, CAP_FIELD_SIZE, 0);
    }

    write_u64_at(&mut signaled, 0, needed_capacity);
    Ok(signaled)
}

fn queue_payload_end(data: &[u8], count: u32) -> io::Result<usize> {
    let mut cursor = HEADER_SIZE;

    for _ in 0..count {
        let size = read_u32_at(data, cursor).ok_or_else(|| {
            io::Error::new(
                io::ErrorKind::InvalidData,
                "invalid storage queue: truncated entry size",
            )
        })? as usize;

        cursor = cursor.checked_add(COUNT_FIELD_SIZE).ok_or_else(|| {
            io::Error::new(
                io::ErrorKind::InvalidData,
                "invalid storage queue: size overflow",
            )
        })?;

        cursor = cursor.checked_add(size).ok_or_else(|| {
            io::Error::new(
                io::ErrorKind::InvalidData,
                "invalid storage queue: size overflow",
            )
        })?;

        if cursor > data.len() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "invalid storage queue: truncated entry payload",
            ));
        }
    }

    Ok(cursor)
}

fn required_queue_size_for_append(queue_bytes: &[u8], entry_size: usize) -> io::Result<usize> {
    let count = read_count(queue_bytes);
    let payload_end = queue_payload_end(queue_bytes, count)?;
    let existing_payload_size = payload_end.saturating_sub(HEADER_SIZE);

    HEADER_SIZE
        .checked_add(existing_payload_size)
        .and_then(|n| n.checked_add(COUNT_FIELD_SIZE))
        .and_then(|n| n.checked_add(entry_size))
        .ok_or_else(|| {
            io::Error::new(
                io::ErrorKind::InvalidInput,
                "storage queue size overflow while appending entry",
            )
        })
}

fn append_storage_entry(queue_bytes: &[u8], entry_bytes: &[u8]) -> io::Result<Vec<u8>> {
    let count = read_count(queue_bytes);
    let payload_end = queue_payload_end(queue_bytes, count)?;
    let existing_payload_size = payload_end.saturating_sub(HEADER_SIZE);

    let entry_size = u32::try_from(entry_bytes.len()).map_err(|_| {
        io::Error::new(
            io::ErrorKind::InvalidInput,
            "intermediate storage payload exceeds u32 size field",
        )
    })?;

    let new_count = count.checked_add(1).ok_or_else(|| {
        io::Error::new(
            io::ErrorKind::InvalidInput,
            "storage queue count overflow while appending entry",
        )
    })?;

    let total_size = required_queue_size_for_append(queue_bytes, entry_bytes.len())?;

    if total_size > queue_bytes.len() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            format!(
                "storage queue requires {total_size} bytes, but shared memory has {} bytes",
                queue_bytes.len()
            ),
        ));
    }

    let mut rewritten = vec![0u8; total_size];
    let needed_capacity = if queue_bytes.len() < CAP_FIELD_SIZE {
        0
    } else {
        read_u64_at(queue_bytes, 0).unwrap_or(0)
    };
    write_u64_at(&mut rewritten, 0, needed_capacity);
    write_u32_at(&mut rewritten, CAP_FIELD_SIZE, new_count);

    if existing_payload_size > 0 {
        rewritten[HEADER_SIZE..HEADER_SIZE + existing_payload_size]
            .copy_from_slice(&queue_bytes[HEADER_SIZE..payload_end]);
    }

    let size_offset = HEADER_SIZE + existing_payload_size;
    write_u32_at(&mut rewritten, size_offset, entry_size);
    let payload_offset = size_offset + COUNT_FIELD_SIZE;
    rewritten[payload_offset..payload_offset + entry_bytes.len()].copy_from_slice(entry_bytes);

    Ok(rewritten)
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

// ---------------------------------------------------------------------------
// Chunking: split a large storage into self-contained queue entries.
//
// The SHM segment backing the storage queue has a fixed size: growing POSIX
// shared memory is impossible on macOS (ftruncate on an already-sized object
// fails with EINVAL), so the grow protocol cannot deliver a payload that
// exceeds the initial 16 MiB segment. Instead, a large result is split into
// multiple IntermediateStorage queue entries that the app merges via
// `PersistentStorage` inject.
//
// Each chunk must be self-contained because inject remaps ids per injected
// storage: edges need their endpoint node rows, source locations need their
// file node (+ file) rows, occurrences need their element (node/edge) and
// location rows within the same chunk. Repeated rows merge on inject —
// nodes dedup by serialized name, edges by (type, source, target), files by
// path, locations by (file, position, type). Occurrences are not deduped,
// so each occurrence is emitted in exactly one chunk; symbol rows are not
// deduped either, so they only accompany their node's home chunk.
// ---------------------------------------------------------------------------

/// Budget for one chunk's (over-)estimated serialized size. Back-pressure
/// keeps at most two entries queued, and two chunks plus queue headers must
/// stay comfortably below the fixed 16 MiB segment.
const CHUNK_BUDGET_BYTES: usize = 7 * 1024 * 1024;

// Conservative per-row estimates of the serialized FlatBuffers footprint
// (table fields + vtable/table overhead + per-row vector offset + padding).
const EDGE_COST: usize = 56;
const SYMBOL_COST: usize = 32;
const LOCATION_COST: usize = 64;
const OCCURRENCE_COST: usize = 32;
const COMPONENT_ACCESS_COST: usize = 32;

fn node_cost(n: &OwnedStorageNode) -> usize {
    48 + n.serialized_name.len()
}

fn file_cost(f: &OwnedStorageFile) -> usize {
    64 + f.file_path.len() + f.language_identifier.len()
}

fn local_symbol_cost(ls: &OwnedStorageLocalSymbol) -> usize {
    48 + ls.name.len()
}

fn error_cost(e: &OwnedStorageError) -> usize {
    64 + e.message.len() + e.translation_unit.len()
}

impl OwnedIntermediateStorage {
    /// Over-estimated serialized size of the whole storage.
    fn estimated_size(&self) -> usize {
        let mut total = 1024;
        total += self.nodes.iter().map(node_cost).sum::<usize>();
        total += self.files.iter().map(file_cost).sum::<usize>();
        total += self.edges.len() * EDGE_COST;
        total += self.symbols.len() * SYMBOL_COST;
        total += self.source_locations.len() * LOCATION_COST;
        total += self
            .local_symbols
            .iter()
            .map(local_symbol_cost)
            .sum::<usize>();
        total += self.occurrences.len() * OCCURRENCE_COST;
        total += self.component_accesses.len() * COMPONENT_ACCESS_COST;
        total += self.errors.iter().map(error_cost).sum::<usize>();
        total
    }

    /// Split this storage into self-contained chunks that each serialize to
    /// well under the SHM segment size. Small storages pass through as a
    /// single chunk unchanged.
    pub fn chunks(&self) -> Vec<OwnedIntermediateStorage> {
        if self.estimated_size() <= CHUNK_BUDGET_BYTES {
            return vec![self.clone()];
        }
        Chunker::new(self).run()
    }
}

struct Chunker<'a> {
    src: &'a OwnedIntermediateStorage,
    nodes_by_id: std::collections::HashMap<i64, &'a OwnedStorageNode>,
    files_by_node_id: std::collections::HashMap<i64, &'a OwnedStorageFile>,
    symbols_by_node_id: std::collections::HashMap<i64, &'a OwnedStorageSymbol>,
    locations_by_id: std::collections::HashMap<i64, &'a OwnedStorageSourceLocation>,
    /// element id → indices into `src.occurrences`, in original order
    occ_indices_by_element: std::collections::HashMap<i64, Vec<usize>>,
    /// locations already emitted into some chunk (for the orphan pass)
    emitted_locations: std::collections::HashSet<i64>,
    chunks: Vec<OwnedIntermediateStorage>,
    cur: OwnedIntermediateStorage,
    cur_cost: usize,
    /// node ids present in the current chunk
    cur_node_ids: std::collections::HashSet<i64>,
    /// location ids present in the current chunk
    cur_location_ids: std::collections::HashSet<i64>,
}

impl<'a> Chunker<'a> {
    fn new(src: &'a OwnedIntermediateStorage) -> Self {
        let nodes_by_id = src.nodes.iter().map(|n| (n.id, n)).collect();
        let files_by_node_id = src.files.iter().map(|f| (f.id, f)).collect();
        let symbols_by_node_id = src.symbols.iter().map(|s| (s.id, s)).collect();
        let locations_by_id = src.source_locations.iter().map(|l| (l.id, l)).collect();
        let mut occ_indices_by_element: std::collections::HashMap<i64, Vec<usize>> =
            std::collections::HashMap::new();
        for (i, o) in src.occurrences.iter().enumerate() {
            occ_indices_by_element
                .entry(o.element_id)
                .or_default()
                .push(i);
        }
        Self {
            src,
            nodes_by_id,
            files_by_node_id,
            symbols_by_node_id,
            locations_by_id,
            occ_indices_by_element,
            emitted_locations: std::collections::HashSet::new(),
            chunks: Vec::new(),
            cur: OwnedIntermediateStorage {
                next_id: src.next_id,
                ..OwnedIntermediateStorage::default()
            },
            cur_cost: 0,
            cur_node_ids: std::collections::HashSet::new(),
            cur_location_ids: std::collections::HashSet::new(),
        }
    }

    fn run(mut self) -> Vec<OwnedIntermediateStorage> {
        let src = self.src;
        // Element groups in stable source order: every group is emitted
        // atomically into one chunk, together with the rows it references.
        for node in &src.nodes {
            let occs = self
                .occ_indices_by_element
                .remove(&node.id)
                .unwrap_or_default();
            self.ensure_budget(self.node_group_cost(node.id, &occs));
            self.add_node(node.id, /*with_symbol=*/ true);
            self.add_occurrences(&occs);
        }
        for edge in &src.edges {
            let occs = self
                .occ_indices_by_element
                .remove(&edge.id)
                .unwrap_or_default();
            self.ensure_budget(self.edge_group_cost(edge, &occs));
            self.add_node(edge.source_node_id, false);
            self.add_node(edge.target_node_id, false);
            self.cur.edges.push(edge.clone());
            self.cur_cost += EDGE_COST;
            self.add_occurrences(&occs);
        }
        for ls in &src.local_symbols {
            let occs = self
                .occ_indices_by_element
                .remove(&ls.id)
                .unwrap_or_default();
            self.ensure_budget(local_symbol_cost(ls) + self.occurrences_cost(&occs));
            self.cur.local_symbols.push(ls.clone());
            self.cur_cost += local_symbol_cost(ls);
            self.add_occurrences(&occs);
        }
        // Dangling occurrences (element not in this storage) — keep them so
        // chunked inject behaves like monolithic inject (warn + drop).
        let dangling: Vec<usize> = {
            let mut v: Vec<usize> = self
                .occ_indices_by_element
                .values()
                .flatten()
                .copied()
                .collect();
            v.sort_unstable();
            v
        };
        for idx in dangling {
            self.ensure_budget(OCCURRENCE_COST + LOCATION_COST + self.stub_cost_max());
            self.add_occurrences(&[idx]);
        }
        // Locations with no occurrence at all.
        for loc in &src.source_locations {
            if self.emitted_locations.contains(&loc.id) {
                continue;
            }
            self.ensure_budget(LOCATION_COST + self.stub_cost_max());
            self.add_location(loc.id);
        }
        for ca in &src.component_accesses {
            self.ensure_budget(COMPONENT_ACCESS_COST + self.stub_cost_max());
            self.add_node(ca.node_id, false);
            self.cur.component_accesses.push(ca.clone());
            self.cur_cost += COMPONENT_ACCESS_COST;
        }
        for err in &src.errors {
            self.ensure_budget(error_cost(err));
            self.cur.errors.push(err.clone());
            self.cur_cost += error_cost(err);
        }
        self.flush();
        self.chunks
    }

    /// Upper bound for one node stub (node row + potential file row).
    fn stub_cost_max(&self) -> usize {
        512
    }

    fn node_group_cost(&self, node_id: i64, occs: &[usize]) -> usize {
        let mut cost = self
            .nodes_by_id
            .get(&node_id)
            .map(|n| node_cost(n))
            .unwrap_or(0)
            + SYMBOL_COST;
        if let Some(f) = self.files_by_node_id.get(&node_id) {
            cost += file_cost(f);
        }
        cost + self.occurrences_cost(occs)
    }

    fn edge_group_cost(&self, edge: &OwnedStorageEdge, occs: &[usize]) -> usize {
        EDGE_COST
            + self.node_group_cost(edge.source_node_id, &[])
            + self.node_group_cost(edge.target_node_id, &[])
            + self.occurrences_cost(occs)
    }

    /// Pessimistic cost of occurrence rows plus their location rows and the
    /// locations' file stubs.
    fn occurrences_cost(&self, occs: &[usize]) -> usize {
        occs.len() * (OCCURRENCE_COST + LOCATION_COST) + occs.len().min(4) * self.stub_cost_max()
    }

    /// Start a new chunk if adding `group_cost` would exceed the budget.
    /// A single over-budget group still goes into one (oversized) chunk —
    /// in practice groups are tiny compared to the budget.
    fn ensure_budget(&mut self, group_cost: usize) {
        if self.cur_cost > 0 && self.cur_cost + group_cost > CHUNK_BUDGET_BYTES {
            self.flush();
        }
    }

    fn flush(&mut self) {
        if self.cur_cost == 0 {
            return;
        }
        let full = std::mem::replace(
            &mut self.cur,
            OwnedIntermediateStorage {
                next_id: self.src.next_id,
                ..OwnedIntermediateStorage::default()
            },
        );
        self.chunks.push(full);
        self.cur_cost = 0;
        self.cur_node_ids.clear();
        self.cur_location_ids.clear();
    }

    /// Add a node row (plus its file row for file nodes) to the current
    /// chunk if not present. `with_symbol` adds the node's symbol row —
    /// only the node's home group does this (symbols are not deduped on
    /// inject).
    fn add_node(&mut self, node_id: i64, with_symbol: bool) {
        if self.cur_node_ids.insert(node_id) {
            if let Some(node) = self.nodes_by_id.get(&node_id).copied() {
                self.cur.nodes.push(node.clone());
                self.cur_cost += node_cost(node);
            }
            if let Some(file) = self.files_by_node_id.get(&node_id).copied() {
                self.cur.files.push(file.clone());
                self.cur_cost += file_cost(file);
            }
        }
        if with_symbol {
            if let Some(sym) = self.symbols_by_node_id.get(&node_id).copied() {
                self.cur.symbols.push(sym.clone());
                self.cur_cost += SYMBOL_COST;
            }
        }
    }

    /// Add a location row (plus its file node stub) to the current chunk if
    /// not present. Locations dedup by position on inject, so a location
    /// repeated in a later chunk merges onto the same id.
    fn add_location(&mut self, location_id: i64) {
        let Some(loc) = self.locations_by_id.get(&location_id).copied() else {
            return;
        };
        if !self.cur_location_ids.insert(location_id) {
            return;
        }
        self.add_node(loc.file_node_id, false);
        self.cur.source_locations.push(loc.clone());
        self.cur_cost += LOCATION_COST;
        self.emitted_locations.insert(location_id);
    }

    fn add_occurrences(&mut self, occ_indices: &[usize]) {
        for &idx in occ_indices {
            let occ = self.src.occurrences[idx].clone();
            self.add_location(occ.source_location_id);
            self.cur.occurrences.push(occ);
            self.cur_cost += OCCURRENCE_COST;
        }
    }
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

    let mut node_offsets: Vec<flatbuffers::WIPOffset<StorageNode>> =
        Vec::with_capacity(s.nodes.len());
    for n in &s.nodes {
        let name = fbb.create_string(&n.serialized_name);
        node_offsets.push(StorageNode::create(
            &mut fbb,
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
            &mut fbb,
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
            &mut fbb,
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
            &mut fbb,
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
            &mut fbb,
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
            &mut fbb,
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
            &mut fbb,
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
            &mut fbb,
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
            &mut fbb,
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

    let storage = IntermediateStorage::create(
        &mut fbb,
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
    );
    let storages_vec = fbb.create_vector(&[storage]);
    let queue = IntermediateStorageQueue::create(
        &mut fbb,
        &IntermediateStorageQueueArgs {
            storages: Some(storages_vec),
        },
    );
    fbb.finish(queue, None);
    fbb.finished_data().to_vec()
}

#[cfg(test)]
mod chunk_tests {
    use super::*;
    use std::collections::{HashMap, HashSet};

    /// Synthetic storage: one file node (id 1) + `n` item nodes, each with a
    /// symbol, a token location + occurrence, and a USAGE edge to the next
    /// node with its own location + occurrence. `name_len` inflates node
    /// names to force multi-chunk splits.
    fn synthetic(n: i64, name_len: usize) -> OwnedIntermediateStorage {
        let mut s = OwnedIntermediateStorage::default();
        s.nodes.push(OwnedStorageNode {
            id: 1,
            type_: 1 << 18,
            serialized_name: "/\tm/tmp/f.rs\ts\tp".to_owned(),
        });
        s.files.push(OwnedStorageFile {
            id: 1,
            file_path: "/tmp/f.rs".to_owned(),
            language_identifier: "rust".to_owned(),
            indexed: true,
            complete: true,
        });
        let mut next = 2;
        let mut node_ids = Vec::new();
        for i in 0..n {
            let id = next;
            next += 1;
            s.nodes.push(OwnedStorageNode {
                id,
                type_: 64,
                serialized_name: format!("n{i}_{}", "x".repeat(name_len)),
            });
            s.symbols.push(OwnedStorageSymbol {
                id,
                definition_kind: 2,
            });
            let loc = next;
            next += 1;
            s.source_locations.push(OwnedStorageSourceLocation {
                id: loc,
                file_node_id: 1,
                start_line: i as u32 + 1,
                start_col: 1,
                end_line: i as u32 + 1,
                end_col: 5,
                type_: 0,
            });
            s.occurrences.push(OwnedStorageOccurrence {
                element_id: id,
                source_location_id: loc,
            });
            node_ids.push(id);
        }
        for w in node_ids.windows(2) {
            let edge = next;
            next += 1;
            s.edges.push(OwnedStorageEdge {
                id: edge,
                type_: 4,
                source_node_id: w[0],
                target_node_id: w[1],
            });
            let loc = next;
            next += 1;
            s.source_locations.push(OwnedStorageSourceLocation {
                id: loc,
                file_node_id: 1,
                start_line: 1,
                start_col: 9,
                end_line: 1,
                end_col: 12,
                type_: 0,
            });
            s.occurrences.push(OwnedStorageOccurrence {
                element_id: edge,
                source_location_id: loc,
            });
        }
        s.errors.push(OwnedStorageError {
            id: next,
            message: "m".to_owned(),
            translation_unit: "/tmp/f.rs".to_owned(),
            fatal: false,
            indexed: true,
        });
        s.next_id = next + 1;
        s
    }

    #[test]
    fn small_storage_passes_through_as_single_chunk() {
        let s = synthetic(10, 8);
        let chunks = s.chunks();
        assert_eq!(chunks.len(), 1);
        let c = &chunks[0];
        assert_eq!(c.nodes.len(), s.nodes.len());
        assert_eq!(c.edges.len(), s.edges.len());
        assert_eq!(c.occurrences.len(), s.occurrences.len());
        assert_eq!(c.source_locations.len(), s.source_locations.len());
        assert_eq!(c.next_id, s.next_id);
    }

    #[test]
    fn large_storage_chunks_are_self_contained_and_cover_everything() {
        // ~2600 nodes x 4 KiB names -> estimate well above the chunk budget.
        let s = synthetic(2600, 4096);
        assert!(s.estimated_size() > CHUNK_BUDGET_BYTES);

        let chunks = s.chunks();
        assert!(chunks.len() > 1, "expected a multi-chunk split");

        let mut seen_occurrences = 0usize;
        let mut seen_edge_ids: HashSet<i64> = HashSet::new();
        let mut seen_symbol_ids: HashSet<i64> = HashSet::new();
        let mut seen_node_names: HashSet<String> = HashSet::new();
        let mut seen_errors = 0usize;

        for chunk in &chunks {
            // Serialized size must stay under the 8 MiB target so two chunks
            // plus queue headers always fit the 16 MiB segment.
            let serialized = serialize_one_storage(chunk);
            assert!(
                serialized.len() < 8 * 1024 * 1024,
                "chunk serializes to {} bytes",
                serialized.len()
            );

            // Self-containment: all intra-chunk references resolve.
            let node_ids: HashSet<i64> = chunk.nodes.iter().map(|n| n.id).collect();
            let edge_ids: HashSet<i64> = chunk.edges.iter().map(|e| e.id).collect();
            let loc_ids: HashSet<i64> = chunk.source_locations.iter().map(|l| l.id).collect();
            for e in &chunk.edges {
                assert!(node_ids.contains(&e.source_node_id));
                assert!(node_ids.contains(&e.target_node_id));
            }
            for l in &chunk.source_locations {
                assert!(node_ids.contains(&l.file_node_id));
            }
            for sym in &chunk.symbols {
                assert!(node_ids.contains(&sym.id));
            }
            for f in &chunk.files {
                assert!(node_ids.contains(&f.id));
            }
            for o in &chunk.occurrences {
                assert!(node_ids.contains(&o.element_id) || edge_ids.contains(&o.element_id));
                assert!(loc_ids.contains(&o.source_location_id));
            }
            assert_eq!(chunk.next_id, s.next_id);

            seen_occurrences += chunk.occurrences.len();
            for e in &chunk.edges {
                assert!(seen_edge_ids.insert(e.id), "edge duplicated across chunks");
            }
            for sym in &chunk.symbols {
                assert!(
                    seen_symbol_ids.insert(sym.id),
                    "symbol duplicated across chunks"
                );
            }
            for n in &chunk.nodes {
                seen_node_names.insert(n.serialized_name.clone());
            }
            seen_errors += chunk.errors.len();
        }

        // Coverage: occurrences exactly once, edges/symbols/errors complete,
        // every node name present somewhere.
        assert_eq!(seen_occurrences, s.occurrences.len());
        assert_eq!(seen_edge_ids.len(), s.edges.len());
        assert_eq!(seen_symbol_ids.len(), s.symbols.len());
        assert_eq!(seen_errors, s.errors.len());
        let original_names: HashSet<String> =
            s.nodes.iter().map(|n| n.serialized_name.clone()).collect();
        assert_eq!(seen_node_names, original_names);

        // Locations must cover every original position exactly (dedup by
        // position on inject makes repeats legal but the synthetic data has
        // unique def positions).
        let original_positions: HashSet<(i64, u32, u32, u32, u32, i32)> = s
            .source_locations
            .iter()
            .map(|l| {
                (
                    l.file_node_id,
                    l.start_line,
                    l.start_col,
                    l.end_line,
                    l.end_col,
                    l.type_,
                )
            })
            .collect();
        let mut chunk_positions: HashSet<(i64, u32, u32, u32, u32, i32)> = HashSet::new();
        for chunk in &chunks {
            for l in &chunk.source_locations {
                chunk_positions.insert((
                    l.file_node_id,
                    l.start_line,
                    l.start_col,
                    l.end_line,
                    l.end_col,
                    l.type_,
                ));
            }
        }
        assert_eq!(chunk_positions, original_positions);

        // The chunker only clones rows — verify nothing was invented.
        let original_node_by_id: HashMap<i64, &OwnedStorageNode> =
            s.nodes.iter().map(|n| (n.id, n)).collect();
        for chunk in &chunks {
            for n in &chunk.nodes {
                assert_eq!(
                    original_node_by_id[&n.id].serialized_name,
                    n.serialized_name
                );
            }
        }
    }
}

#[cfg(test)]
mod queue_tests {
    use super::*;

    #[test]
    fn append_storage_entry_writes_cpp_compatible_header() {
        let queue = vec![0u8; 256];
        let entry = vec![1u8, 2u8, 3u8];

        let rewritten = append_storage_entry(&queue, &entry).unwrap();

        assert_eq!(
            rewritten.len(),
            HEADER_SIZE + COUNT_FIELD_SIZE + entry.len()
        );
        assert_eq!(read_u64_at(&rewritten, 0), Some(0));
        assert_eq!(read_count(&rewritten), 1);
        assert_eq!(
            read_u32_at(&rewritten, HEADER_SIZE),
            Some(entry.len() as u32)
        );
        assert_eq!(
            &rewritten
                [HEADER_SIZE + COUNT_FIELD_SIZE..HEADER_SIZE + COUNT_FIELD_SIZE + entry.len()],
            entry.as_slice()
        );
    }

    #[test]
    fn append_storage_entry_preserves_existing_payload_and_increments_count() {
        let first = append_storage_entry(&vec![0u8; 256], &[9u8, 8u8]).unwrap();

        let mut queue = vec![0u8; 256];
        queue[..first.len()].copy_from_slice(&first);

        let rewritten = append_storage_entry(&queue, &[7u8]).unwrap();

        assert_eq!(read_count(&rewritten), 2);

        let first_size = read_u32_at(&rewritten, HEADER_SIZE).unwrap() as usize;
        assert_eq!(first_size, 2);
        assert_eq!(
            &rewritten[HEADER_SIZE + COUNT_FIELD_SIZE..HEADER_SIZE + COUNT_FIELD_SIZE + first_size],
            &[9u8, 8u8]
        );

        let second_size_offset = HEADER_SIZE + COUNT_FIELD_SIZE + first_size;
        assert_eq!(read_u32_at(&rewritten, second_size_offset), Some(1));
    }

    #[test]
    fn append_storage_entry_rejects_truncated_existing_payload() {
        let mut invalid = vec![0u8; HEADER_SIZE + COUNT_FIELD_SIZE + 1];
        write_u32_at(&mut invalid, CAP_FIELD_SIZE, 1);
        write_u32_at(&mut invalid, HEADER_SIZE, 4);

        let err = append_storage_entry(&invalid, &[1u8]).unwrap_err();

        assert_eq!(err.kind(), io::ErrorKind::InvalidData);
    }

    #[test]
    fn needed_capacity_signal_preserves_queue_count_and_entries() {
        let first = append_storage_entry(&vec![0u8; 256], &[1u8, 2u8, 3u8]).unwrap();
        let mut queue = vec![0u8; 256];
        queue[..first.len()].copy_from_slice(&first);

        let signaled = with_needed_capacity_signal(&queue, 512).unwrap();

        assert_eq!(read_u64_at(&signaled, 0), Some(512));
        assert_eq!(read_count(&signaled), 1);
        assert_eq!(read_u32_at(&signaled, HEADER_SIZE), Some(3));
        assert_eq!(
            &signaled[HEADER_SIZE + COUNT_FIELD_SIZE..HEADER_SIZE + COUNT_FIELD_SIZE + 3],
            &[1u8, 2u8, 3u8]
        );
    }
}
