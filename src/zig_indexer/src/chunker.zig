//! Split an oversized `Storage` into several self-contained `Chunk`s, each of
//! which serializes to a single `IntermediateStorage` message that fits the SHM
//! segment. A direct port of the Rust `ipc/storage.rs` chunker and the Swift
//! `StorageChunker` (same 7 MiB budget, same grouping).
//!
//! The app's PersistentStorage re-maps element ids per injected message and
//! dedups nodes by serialized name, edges by (type, source, target), files by
//! path, and source locations by (file, position, type) — but NOT occurrences
//! or symbols. So each chunk must be self-contained: an edge carries stub copies
//! of its endpoint nodes, an occurrence carries its source location (and that
//! location's file node), and every occurrence/symbol is emitted exactly once.
//! Repeating a referenced node as a stub across chunks is safe (dedup on
//! inject); emitting an occurrence twice would double it, so we don't.

const std = @import("std");
const storage = @import("storage.zig");
const Storage = storage.Storage;
const Chunk = storage.Chunk;
const Id = storage.Id;

/// Per-chunk byte budget. Two chunks plus the queue headers stay comfortably
/// under the 16 MiB segment, and back-pressure keeps at most two entries queued
/// — identical to the Rust/Swift budget.
pub const budget_bytes: usize = 7 * 1024 * 1024;

// Conservative FlatBuffers per-row footprint estimates (over-estimates on
// purpose), matching the Rust/Swift cost model.
const edge_cost: usize = 56;
const symbol_cost: usize = 32;
const location_cost: usize = 64;
const occurrence_cost: usize = 32;
const component_access_cost: usize = 32;
const stub_cost_max: usize = 512;

fn nodeCost(n: storage.StorageNode) usize {
    return 48 + n.serialized_name.len;
}
fn fileCost(f: storage.StorageFile) usize {
    return 64 + f.file_path.len + f.language_identifier.len;
}
fn localSymbolCost(ls: storage.StorageLocalSymbol) usize {
    return 48 + ls.name.len;
}
fn errorCost(e: storage.StorageError) usize {
    return 64 + e.message.len + e.translation_unit.len;
}

/// Over-estimate the serialized size of the whole store (starts at 1 KiB for the
/// buffer/table overhead). Used to skip chunking when the store already fits.
pub fn estimatedSize(store: *const Storage) usize {
    var total: usize = 1024;
    for (store.nodes.items) |n| total += nodeCost(n) + symbol_cost;
    for (store.files.items) |f| total += fileCost(f);
    total += store.edges.items.len * edge_cost;
    total += store.source_locations.items.len * location_cost;
    total += store.occurrences.items.len * occurrence_cost;
    total += store.component_accesses.items.len * component_access_cost;
    for (store.local_symbols.items) |ls| total += localSymbolCost(ls);
    for (store.errors.items) |e| total += errorCost(e);
    return total;
}

/// Owns the arena backing the returned chunks; `deinit` frees them all.
pub const ChunkSet = struct {
    arena: std.heap.ArenaAllocator,
    items: []Chunk,

    pub fn deinit(self: *ChunkSet) void {
        self.arena.deinit();
    }
};

/// Split `store` into serializable chunks. When it already fits the budget the
/// result is a single chunk borrowing the store's rows (no copies).
pub fn chunk(gpa: std.mem.Allocator, store: *const Storage) !ChunkSet {
    var arena = std.heap.ArenaAllocator.init(gpa);
    errdefer arena.deinit();

    if (estimatedSize(store) <= budget_bytes) {
        const one = try arena.allocator().alloc(Chunk, 1);
        one[0] = store.wholeChunk();
        return .{ .arena = arena, .items = one };
    }

    var chunker = try Chunker.init(&arena, store);
    const items = try chunker.run();
    return .{ .arena = arena, .items = items };
}

const IdSet = std.AutoHashMapUnmanaged(Id, void);
const IdList = std.AutoHashMapUnmanaged(Id, std.ArrayListUnmanaged(usize));

const Chunker = struct {
    a: std.mem.Allocator,
    store: *const Storage,

    // Lookup indices into the store's row lists, by id.
    node_by_id: std.AutoHashMapUnmanaged(Id, usize) = .empty,
    file_by_id: std.AutoHashMapUnmanaged(Id, usize) = .empty,
    symbol_by_id: std.AutoHashMapUnmanaged(Id, usize) = .empty,
    location_by_id: std.AutoHashMapUnmanaged(Id, usize) = .empty,
    occ_by_element: IdList = .empty,

    // The chunk currently being filled.
    out: std.ArrayListUnmanaged(Chunk) = .empty,
    cur: Cur = .{},

    const Cur = struct {
        nodes: std.ArrayListUnmanaged(storage.StorageNode) = .empty,
        files: std.ArrayListUnmanaged(storage.StorageFile) = .empty,
        edges: std.ArrayListUnmanaged(storage.StorageEdge) = .empty,
        symbols: std.ArrayListUnmanaged(storage.StorageSymbol) = .empty,
        locations: std.ArrayListUnmanaged(storage.StorageSourceLocation) = .empty,
        local_symbols: std.ArrayListUnmanaged(storage.StorageLocalSymbol) = .empty,
        occurrences: std.ArrayListUnmanaged(storage.StorageOccurrence) = .empty,
        component_accesses: std.ArrayListUnmanaged(storage.StorageComponentAccess) = .empty,
        errors: std.ArrayListUnmanaged(storage.StorageError) = .empty,
        node_ids: IdSet = .empty,
        location_ids: IdSet = .empty,
        cost: usize = 0,
    };

    fn init(arena: *std.heap.ArenaAllocator, store: *const Storage) !Chunker {
        var self = Chunker{ .a = arena.allocator(), .store = store };
        for (store.nodes.items, 0..) |n, i| try self.node_by_id.put(self.a, n.id, i);
        for (store.files.items, 0..) |f, i| try self.file_by_id.put(self.a, f.id, i);
        for (store.symbols.items, 0..) |s, i| try self.symbol_by_id.put(self.a, s.id, i);
        for (store.source_locations.items, 0..) |l, i| try self.location_by_id.put(self.a, l.id, i);
        for (store.occurrences.items, 0..) |o, i| {
            const gop = try self.occ_by_element.getOrPut(self.a, o.element_id);
            if (!gop.found_existing) gop.value_ptr.* = .empty;
            try gop.value_ptr.append(self.a, i);
        }
        return self;
    }

    fn run(self: *Chunker) ![]Chunk {
        // Nodes (home group): the node + its symbol + file row + occurrences.
        for (self.store.nodes.items) |n| {
            self.ensureBudget(self.nodeGroupCost(n));
            try self.addNode(n.id, true);
            try self.addOccurrences(n.id);
        }
        // Edges: stub both endpoints, the edge, and the edge's occurrences.
        for (self.store.edges.items) |e| {
            self.ensureBudget(2 * stub_cost_max + edge_cost + self.occurrencesCost(e.id));
            try self.addNode(e.source_node_id, false);
            try self.addNode(e.target_node_id, false);
            try self.cur.edges.append(self.a, e);
            self.cur.cost += edge_cost;
            try self.addOccurrences(e.id);
        }
        // Local symbols and their occurrences.
        for (self.store.local_symbols.items) |ls| {
            self.ensureBudget(localSymbolCost(ls) + self.occurrencesCost(ls.id));
            try self.cur.local_symbols.append(self.a, ls);
            self.cur.cost += localSymbolCost(ls);
            try self.addOccurrences(ls.id);
        }
        // Occurrences whose element wasn't emitted above (defensive; the Zig
        // passes always create an element per occurrence, so normally none).
        for (self.store.occurrences.items) |o| {
            const is_element = self.node_by_id.contains(o.element_id) or self.symbol_by_id.contains(o.element_id) //
            or blk: {
                for (self.store.edges.items) |e| if (e.id == o.element_id) break :blk true;
                for (self.store.local_symbols.items) |ls| if (ls.id == o.element_id) break :blk true;
                break :blk false;
            };
            if (is_element) continue;
            self.ensureBudget(occurrence_cost + location_cost + stub_cost_max);
            try self.addLocation(o.source_location_id);
            try self.cur.occurrences.append(self.a, o);
            self.cur.cost += occurrence_cost;
        }
        // Source locations with no occurrence referencing them (defensive).
        for (self.store.source_locations.items) |l| {
            if (self.cur.location_ids.contains(l.id)) continue;
            var referenced = false;
            for (self.store.occurrences.items) |o| {
                if (o.source_location_id == l.id) {
                    referenced = true;
                    break;
                }
            }
            if (referenced) continue;
            self.ensureBudget(location_cost + stub_cost_max);
            try self.addLocation(l.id);
        }
        // Component accesses: each with a stub of the node it annotates.
        for (self.store.component_accesses.items) |ca| {
            self.ensureBudget(stub_cost_max + component_access_cost);
            try self.addNode(ca.node_id, false);
            try self.cur.component_accesses.append(self.a, ca);
            self.cur.cost += component_access_cost;
        }
        // Errors last.
        for (self.store.errors.items) |e| {
            self.ensureBudget(errorCost(e));
            try self.cur.errors.append(self.a, e);
            self.cur.cost += errorCost(e);
        }
        try self.flush();
        return self.out.items;
    }

    /// Flush the current chunk if adding a `group_cost`-byte group would push a
    /// non-empty chunk over budget (an oversized single group still goes alone).
    fn ensureBudget(self: *Chunker, group_cost: usize) void {
        if (self.cur.cost > 0 and self.cur.cost + group_cost > budget_bytes) {
            self.flush() catch {};
        }
    }

    fn nodeGroupCost(self: *Chunker, n: storage.StorageNode) usize {
        var cost = nodeCost(n) + symbol_cost;
        if (self.file_by_id.get(n.id)) |i| cost += fileCost(self.store.files.items[i]);
        return cost + self.occurrencesCost(n.id);
    }

    fn occurrencesCost(self: *Chunker, element_id: Id) usize {
        const list = self.occ_by_element.get(element_id) orelse return 0;
        const count = list.items.len;
        return count * (occurrence_cost + location_cost) + @min(count, 4) * stub_cost_max;
    }

    /// Add a node row to the current chunk (deduped within the chunk). Pulls in
    /// its file row when it's a file node, and its symbol row when `with_symbol`.
    fn addNode(self: *Chunker, node_id: Id, with_symbol: bool) !void {
        const idx = self.node_by_id.get(node_id) orelse return;
        if ((try self.cur.node_ids.getOrPut(self.a, node_id)).found_existing) return;
        const n = self.store.nodes.items[idx];
        try self.cur.nodes.append(self.a, n);
        self.cur.cost += nodeCost(n);
        if (self.file_by_id.get(node_id)) |fi| {
            try self.cur.files.append(self.a, self.store.files.items[fi]);
            self.cur.cost += fileCost(self.store.files.items[fi]);
        }
        if (with_symbol) {
            if (self.symbol_by_id.get(node_id)) |si| {
                try self.cur.symbols.append(self.a, self.store.symbols.items[si]);
                self.cur.cost += symbol_cost;
            }
        }
    }

    fn addLocation(self: *Chunker, loc_id: Id) !void {
        if ((try self.cur.location_ids.getOrPut(self.a, loc_id)).found_existing) return;
        const idx = self.location_by_id.get(loc_id) orelse return;
        const l = self.store.source_locations.items[idx];
        try self.cur.locations.append(self.a, l);
        self.cur.cost += location_cost;
        // The location references a file node; stub it so the app can remap it.
        try self.addNode(l.file_node_id, false);
    }

    fn addOccurrences(self: *Chunker, element_id: Id) !void {
        const list = self.occ_by_element.get(element_id) orelse return;
        for (list.items) |oi| {
            const o = self.store.occurrences.items[oi];
            try self.addLocation(o.source_location_id);
            try self.cur.occurrences.append(self.a, o);
            self.cur.cost += occurrence_cost;
        }
    }

    /// Freeze the current chunk into the output and start a fresh one.
    fn flush(self: *Chunker) !void {
        if (self.cur.nodes.items.len == 0 and self.cur.edges.items.len == 0 and
            self.cur.local_symbols.items.len == 0 and self.cur.occurrences.items.len == 0 and
            self.cur.locations.items.len == 0 and self.cur.component_accesses.items.len == 0 and
            self.cur.errors.items.len == 0) return;
        try self.out.append(self.a, .{
            .next_id = self.store.next_id,
            .nodes = self.cur.nodes.items,
            .files = self.cur.files.items,
            .edges = self.cur.edges.items,
            .symbols = self.cur.symbols.items,
            .source_locations = self.cur.locations.items,
            .local_symbols = self.cur.local_symbols.items,
            .occurrences = self.cur.occurrences.items,
            .component_accesses = self.cur.component_accesses.items,
            .errors = self.cur.errors.items,
        });
        self.cur = .{}; // the old lists live in the arena, referenced by the frozen chunk
    }
};
