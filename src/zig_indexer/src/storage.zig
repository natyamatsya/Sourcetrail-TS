//! Owned, in-memory mirror of Sourcetrail's `IntermediateStorage` — the payload
//! every indexer produces. Mirrors the Rust `OwnedIntermediateStorage` and the
//! FlatBuffers tables in
//! `src/lib/data/indexer/interprocess/schemas/intermediate_storage.fbs`.
//!
//! Enum integer values are copied verbatim from the C++ source of truth:
//!   NodeKind      -> src/lib/data/NodeKind.h        (bitmask, int32)
//!   EdgeType      -> src/lib/data/graph/Edge.h       (bitmask, int32)
//!   LocationType  -> src/lib/data/location/LocationType.h (0..9)
//!   DefinitionKind-> src/lib/data/DefinitionKind.h   (0..2)
//! They MUST stay in lockstep with those headers — the wire is int32.

const std = @import("std");
const Allocator = std.mem.Allocator;

/// NodeKind bitmask — src/lib/data/NodeKind.h.
pub const NodeKind = enum(i32) {
    undefined = 0,
    symbol = 1 << 0,
    type = 1 << 1,
    builtin_type = 1 << 2,
    module = 1 << 3,
    namespace = 1 << 4,
    package = 1 << 5,
    @"struct" = 1 << 6,
    class = 1 << 7,
    interface = 1 << 8,
    annotation = 1 << 9,
    global_variable = 1 << 10,
    field = 1 << 11,
    function = 1 << 12,
    method = 1 << 13,
    @"enum" = 1 << 14,
    enum_constant = 1 << 15,
    typedef = 1 << 16,
    type_parameter = 1 << 17,
    file = 1 << 18,
    macro = 1 << 19,
    @"union" = 1 << 20,
    record = 1 << 21,
    concept = 1 << 22,
};

/// Edge::EdgeType bitmask — src/lib/data/graph/Edge.h.
pub const EdgeType = enum(i32) {
    undefined = 0,
    member = 1 << 0,
    type_usage = 1 << 1,
    usage = 1 << 2,
    call = 1 << 3,
    inheritance = 1 << 4,
    override = 1 << 5,
    type_argument = 1 << 6,
    template_specialization = 1 << 7,
    include = 1 << 8,
    import = 1 << 9,
    bundled_edges = 1 << 10,
    macro_usage = 1 << 11,
    annotation_usage = 1 << 12,
};

/// LocationType — src/lib/data/location/LocationType.h.
pub const LocationType = enum(i32) {
    token = 0,
    scope = 1,
    qualifier = 2,
    local_symbol = 3,
    signature = 4,
    comment = 5,
    err = 6,
    fulltext_search = 7,
    screen_search = 8,
    unsolved = 9,
};

/// DefinitionKind — src/lib/data/DefinitionKind.h.
pub const DefinitionKind = enum(i32) {
    none = 0,
    implicit = 1,
    explicit = 2,
};

pub const Id = i64;

/// Globally-unique serialized name for a symbol: `"<file>::<local dotted path>"`.
/// Zig files are anonymous structs with no global namespace, so a bare local
/// name (`square`) collides across files; qualifying by the defining file makes
/// it unique. BOTH the declaration pass (parser) and the ZLS reference pass must
/// build names this way so a resolved cross-file target dedups onto the exact
/// node the target file's declaration pass created.
pub fn qualifiedName(a: std.mem.Allocator, file: []const u8, local: []const u8) ![]u8 {
    return std.fmt.allocPrint(a, "{s}::{s}", .{ file, local });
}

pub const StorageNode = struct { id: Id, kind: NodeKind, serialized_name: []const u8, modifiers: i32 = 0 };
pub const StorageFile = struct { id: Id, file_path: []const u8, language_identifier: []const u8, indexed: bool, complete: bool };
pub const StorageEdge = struct { id: Id, kind: EdgeType, source_node_id: Id, target_node_id: Id };
pub const StorageSymbol = struct { id: Id, definition_kind: DefinitionKind };
pub const StorageSourceLocation = struct {
    id: Id,
    file_node_id: Id,
    start_line: u32,
    start_col: u32,
    end_line: u32,
    end_col: u32,
    kind: LocationType,
};
pub const StorageLocalSymbol = struct { id: Id, name: []const u8 };
pub const StorageOccurrence = struct { element_id: Id, source_location_id: Id };
pub const StorageError = struct { id: Id, message: []const u8, translation_unit: []const u8, fatal: bool, indexed: bool };

/// A 1-based source span (Sourcetrail lines/cols are 1-based, end inclusive).
pub const Span = struct { start_line: u32, start_col: u32, end_line: u32, end_col: u32 };

/// Accumulates rows and hands out element ids from a single counter, exactly
/// like the C++ `IntermediateStorage::createId()` path (ids start at 1).
pub const Storage = struct {
    arena: std.heap.ArenaAllocator,
    next_id: Id = 1,

    nodes: std.ArrayListUnmanaged(StorageNode) = .empty,
    files: std.ArrayListUnmanaged(StorageFile) = .empty,
    edges: std.ArrayListUnmanaged(StorageEdge) = .empty,
    symbols: std.ArrayListUnmanaged(StorageSymbol) = .empty,
    source_locations: std.ArrayListUnmanaged(StorageSourceLocation) = .empty,
    local_symbols: std.ArrayListUnmanaged(StorageLocalSymbol) = .empty,
    occurrences: std.ArrayListUnmanaged(StorageOccurrence) = .empty,
    errors: std.ArrayListUnmanaged(StorageError) = .empty,

    /// De-dup: serialized_name -> node id (a symbol seen from multiple sites is
    /// one node), mirroring the C++ `serialized_name` UNIQUE constraint.
    node_by_name: std.StringHashMapUnmanaged(Id) = .empty,

    pub fn init(child: Allocator) Storage {
        return .{ .arena = std.heap.ArenaAllocator.init(child) };
    }

    pub fn deinit(self: *Storage) void {
        self.arena.deinit();
    }

    fn alloc(self: *Storage) Allocator {
        return self.arena.allocator();
    }

    fn createId(self: *Storage) Id {
        const id = self.next_id;
        self.next_id += 1;
        return id;
    }

    pub fn recordFile(self: *Storage, path: []const u8, language: []const u8, indexed: bool) !Id {
        const a = self.alloc();
        const id = self.createId();
        try self.files.append(a, .{
            .id = id,
            .file_path = try a.dupe(u8, path),
            .language_identifier = try a.dupe(u8, language),
            .indexed = indexed,
            .complete = true,
        });
        // A file is also a node (NODE_FILE) keyed by its path, matching the C++ model.
        try self.nodes.append(a, .{ .id = id, .kind = .file, .serialized_name = try a.dupe(u8, path) });
        try self.node_by_name.put(a, path, id);
        return id;
    }

    /// Record (or dedup) a symbol node by serialized name. `def` marks it an
    /// explicit/implicit definition (a StorageSymbol row) when non-null.
    pub fn recordNode(self: *Storage, kind: NodeKind, serialized_name: []const u8, def: ?DefinitionKind) !Id {
        const a = self.alloc();
        if (self.node_by_name.get(serialized_name)) |existing| return existing;
        const id = self.createId();
        const owned = try a.dupe(u8, serialized_name);
        try self.nodes.append(a, .{ .id = id, .kind = kind, .serialized_name = owned });
        try self.node_by_name.put(a, owned, id);
        if (def) |d| try self.symbols.append(a, .{ .id = id, .definition_kind = d });
        return id;
    }

    pub fn recordEdge(self: *Storage, kind: EdgeType, source: Id, target: Id) !Id {
        const a = self.alloc();
        const id = self.createId();
        try self.edges.append(a, .{ .id = id, .kind = kind, .source_node_id = source, .target_node_id = target });
        return id;
    }

    pub fn recordLocation(self: *Storage, element_id: Id, file_node_id: Id, span: Span, kind: LocationType) !Id {
        const a = self.alloc();
        const id = self.createId();
        try self.source_locations.append(a, .{
            .id = id,
            .file_node_id = file_node_id,
            .start_line = span.start_line,
            .start_col = span.start_col,
            .end_line = span.end_line,
            .end_col = span.end_col,
            .kind = kind,
        });
        try self.occurrences.append(a, .{ .element_id = element_id, .source_location_id = id });
        return id;
    }

    pub fn recordError(self: *Storage, message: []const u8, translation_unit: []const u8, fatal: bool) !Id {
        const a = self.alloc();
        const id = self.createId();
        try self.errors.append(a, .{
            .id = id,
            .message = try a.dupe(u8, message),
            .translation_unit = try a.dupe(u8, translation_unit),
            .fatal = fatal,
            .indexed = true,
        });
        return id;
    }
};
