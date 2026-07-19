const std = @import("std");
const testing = std.testing;
const indexer = @import("indexer");
const storage = indexer.storage;
const NodeKind = storage.NodeKind;
const EdgeType = storage.EdgeType;

fn indexString(gpa: std.mem.Allocator, store: *storage.Storage, src: [:0]const u8) !void {
    try indexer.parser.indexSource(gpa, store, "test.zig", src);
}

/// Find the node for a symbol declared in "test.zig" by its dotted local path
/// (e.g. "Point.norm"); null if absent. Rebuilds the exact NameHierarchy wire
/// name storage.qualifiedName would produce and matches on it.
fn nodeNamed(store: *storage.Storage, local: []const u8) ?storage.StorageNode {
    const expected = storage.qualifiedName(store.arena.allocator(), "test.zig", local) catch return null;
    for (store.nodes.items) |n| {
        if (std.mem.eql(u8, n.serialized_name, expected)) return n;
    }
    return null;
}

fn countEdges(store: *storage.Storage, kind: EdgeType) usize {
    var n: usize = 0;
    for (store.edges.items) |e| {
        if (e.kind == kind) n += 1;
    }
    return n;
}

test "top-level function is a NODE_FUNCTION" {
    var store = storage.Storage.init(testing.allocator);
    defer store.deinit();
    try indexString(testing.allocator, &store,
        \\pub fn add(a: i32, b: i32) i32 {
        \\    return a + b;
        \\}
    );
    const add = nodeNamed(&store, "add") orelse return error.MissingNode;
    try testing.expectEqual(NodeKind.function, add.kind);
}

test "struct with fields and methods" {
    var store = storage.Storage.init(testing.allocator);
    defer store.deinit();
    try indexString(testing.allocator, &store,
        \\const Point = struct {
        \\    x: i32,
        \\    y: i32,
        \\    pub fn norm(self: Point) i32 {
        \\        return self.x + self.y;
        \\    }
        \\};
    );
    try testing.expectEqual(NodeKind.@"struct", (nodeNamed(&store, "Point") orelse return error.MissingNode).kind);
    try testing.expectEqual(NodeKind.field, (nodeNamed(&store, "Point.x") orelse return error.MissingNode).kind);
    try testing.expectEqual(NodeKind.field, (nodeNamed(&store, "Point.y") orelse return error.MissingNode).kind);
    try testing.expectEqual(NodeKind.method, (nodeNamed(&store, "Point.norm") orelse return error.MissingNode).kind);
    // Point owns 3 members (x, y, norm).
    try testing.expectEqual(@as(usize, 3), countEdges(&store, .member));
}

test "enum members are NODE_ENUM_CONSTANT" {
    var store = storage.Storage.init(testing.allocator);
    defer store.deinit();
    try indexString(testing.allocator, &store,
        \\const Color = enum {
        \\    red,
        \\    green,
        \\    blue,
        \\};
    );
    try testing.expectEqual(NodeKind.@"enum", (nodeNamed(&store, "Color") orelse return error.MissingNode).kind);
    try testing.expectEqual(NodeKind.enum_constant, (nodeNamed(&store, "Color.red") orelse return error.MissingNode).kind);
    try testing.expectEqual(NodeKind.enum_constant, (nodeNamed(&store, "Color.blue") orelse return error.MissingNode).kind);
}

test "@import binding is recorded; the resolved EDGE_IMPORT is the semantic pass's job" {
    var store = storage.Storage.init(testing.allocator);
    defer store.deinit();
    try indexString(testing.allocator, &store,
        \\const std = @import("std");
        \\const util = @import("util.zig");
    );
    // The syntactic pass records the import bindings but emits NO import edge —
    // a raw "util.zig" node would be a phantom file that breaks incremental
    // reverse-dep. The real EDGE_IMPORT (to the resolved absolute path) is
    // emitted by semantic.resolveImports (needs ZLS; see src/ipc smoke).
    try testing.expectEqual(@as(usize, 0), countEdges(&store, .import));
    try testing.expectEqual(NodeKind.global_variable, (nodeNamed(&store, "std") orelse return error.MissingNode).kind);
    try testing.expectEqual(NodeKind.global_variable, (nodeNamed(&store, "util") orelse return error.MissingNode).kind);
}

test "container-scope const/var is a NODE_GLOBAL_VARIABLE" {
    var store = storage.Storage.init(testing.allocator);
    defer store.deinit();
    try indexString(testing.allocator, &store,
        \\const MAX: usize = 100;
        \\var counter: usize = 0;
    );
    try testing.expectEqual(NodeKind.global_variable, (nodeNamed(&store, "MAX") orelse return error.MissingNode).kind);
    try testing.expectEqual(NodeKind.global_variable, (nodeNamed(&store, "counter") orelse return error.MissingNode).kind);
}

test "definition nodes carry a name TOKEN and a SCOPE location" {
    var store = storage.Storage.init(testing.allocator);
    defer store.deinit();
    try indexString(testing.allocator, &store,
        \\pub fn foo() void {}
    );
    const foo = nodeNamed(&store, "foo") orelse return error.MissingNode;
    var token_locs: usize = 0;
    var scope_locs: usize = 0;
    for (store.occurrences.items) |occ| {
        if (occ.element_id != foo.id) continue;
        for (store.source_locations.items) |loc| {
            if (loc.id != occ.source_location_id) continue;
            switch (loc.kind) {
                .token => {
                    token_locs += 1;
                    // "foo" starts at line 1, col 8 (1-based).
                    try testing.expectEqual(@as(u32, 1), loc.start_line);
                    try testing.expectEqual(@as(u32, 8), loc.start_col);
                    try testing.expectEqual(@as(u32, 10), loc.end_col);
                },
                .scope => scope_locs += 1,
                else => {},
            }
        }
    }
    try testing.expectEqual(@as(usize, 1), token_locs);
    try testing.expectEqual(@as(usize, 1), scope_locs);
}

test "parse error is recorded, not fatal to indexing" {
    var store = storage.Storage.init(testing.allocator);
    defer store.deinit();
    try indexString(testing.allocator, &store,
        \\pub fn broken( {
    );
    try testing.expect(store.errors.items.len > 0);
}

test "file-as-struct: top-level fields are members of the file node" {
    const a = testing.allocator;
    var store = storage.Storage.init(a);
    defer store.deinit();
    // A .zig file used as a struct: fields declared at file scope.
    try indexString(a, &store,
        \\x: i32,
        \\y: i32,
        \\pub fn sum(self: @This()) i32 { return self.x + self.y; }
    );
    // The file node is the owner; x and y each get an EDGE_MEMBER from it.
    const file_id = store.node_by_name.get("test.zig").?;
    var member_from_file: usize = 0;
    for (store.edges.items) |e| {
        if (e.kind == .member and e.source_node_id == file_id) member_from_file += 1;
    }
    try testing.expectEqual(@as(usize, 2), member_from_file);
}

test "component access: pub -> public, private -> default, type param -> type_parameter" {
    const a = testing.allocator;
    var store = storage.Storage.init(a);
    defer store.deinit();
    try indexString(a, &store,
        \\pub fn exported(comptime T: type) void { _ = T; }
        \\fn internal() void {}
    );
    const access = struct {
        fn of(s: *storage.Storage, local: []const u8) ?storage.AccessKind {
            const n = nodeNamed(s, local) orelse return null;
            for (s.component_accesses.items) |ca| {
                if (ca.node_id == n.id) return ca.access;
            }
            return null;
        }
    }.of;
    try testing.expectEqual(storage.AccessKind.public, access(&store, "exported").?);
    try testing.expectEqual(storage.AccessKind.default, access(&store, "internal").?);
    try testing.expectEqual(storage.AccessKind.type_parameter, access(&store, "exported.T").?);
    // One access row per node (dedup).
    try testing.expectEqual(store.nodes.items.len - 1, store.component_accesses.items.len); // all but the file node
}

test "generic function's comptime type parameter is a NODE_TYPE_PARAMETER member" {
    const a = testing.allocator;
    var store = storage.Storage.init(a);
    defer store.deinit();
    try indexString(a, &store,
        \\pub fn List(comptime T: type, count: usize) type {
        \\    return struct { items: [count]T };
        \\}
    );
    const list = nodeNamed(&store, "List") orelse return error.MissingNode;
    try testing.expectEqual(NodeKind.function, list.kind);
    const tparam = nodeNamed(&store, "List.T") orelse return error.MissingNode;
    try testing.expectEqual(NodeKind.type_parameter, tparam.kind);
    // `count: usize` is a value parameter, not a type parameter.
    try testing.expect(nodeNamed(&store, "List.count") == null);
    // T is a member of List.
    var member = false;
    for (store.edges.items) |e| {
        if (e.kind == .member and e.source_node_id == list.id and e.target_node_id == tparam.id) member = true;
    }
    try testing.expect(member);
}

test "local symbols dedup by name; each reference is a type-3 occurrence" {
    const a = testing.allocator;
    var store = storage.Storage.init(a);
    defer store.deinit();
    const file_id = try store.recordFile("test.zig", "zig", true);

    // Two references to the same local (same declaration site) share one row.
    const name = "test.zig<3:9>";
    const id1 = try store.recordLocalSymbol(name);
    _ = try store.recordLocation(id1, file_id, .{ .start_line = 3, .start_col = 9, .end_line = 3, .end_col = 9 }, .local_symbol);
    const id2 = try store.recordLocalSymbol(name);
    _ = try store.recordLocation(id2, file_id, .{ .start_line = 5, .start_col = 12, .end_line = 5, .end_col = 12 }, .local_symbol);

    try testing.expectEqual(id1, id2);
    try testing.expectEqual(@as(usize, 1), store.local_symbols.items.len);
    var type3: usize = 0;
    for (store.source_locations.items) |l| {
        if (l.kind == .local_symbol) type3 += 1;
    }
    try testing.expectEqual(@as(usize, 2), type3);
}

test "chunker: small store is one borrowed chunk" {
    const a = testing.allocator;
    var store = storage.Storage.init(a);
    defer store.deinit();
    try indexString(a, &store,
        \\const Point = struct { x: i32, pub fn f(self: Point) i32 { return self.x; } };
    );
    var set = try indexer.chunker.chunk(a, &store);
    defer set.deinit();
    try testing.expectEqual(@as(usize, 1), set.items.len);
    try testing.expectEqual(store.nodes.items.len, set.items[0].nodes.len);
    try testing.expectEqual(store.edges.items.len, set.items[0].edges.len);
}

test "chunker: oversized store splits into self-contained chunks" {
    const a = testing.allocator;
    var store = storage.Storage.init(a);
    defer store.deinit();

    const file_id = try store.recordFile("big.zig", "zig", true);
    // ~8 KB names so a store of 1200 nodes far exceeds the 7 MiB budget.
    const big_name = "n" ** 8000;
    var buf: [8064]u8 = undefined;
    var prev: ?storage.Id = null;
    var k: usize = 0;
    while (k < 1200) : (k += 1) {
        const name = try std.fmt.bufPrint(&buf, "{s}{d}", .{ big_name, k });
        const id = try store.recordNode(.function, name, .explicit);
        _ = try store.recordLocation(id, file_id, .{ .start_line = 1, .start_col = 1, .end_line = 1, .end_col = 2 }, .token);
        if (prev) |p| _ = try store.recordEdge(.call, p, id);
        prev = id;
    }

    var set = try indexer.chunker.chunk(a, &store);
    defer set.deinit();
    try testing.expect(set.items.len >= 2);

    var total_occurrences: usize = 0;
    var total_edges: usize = 0;
    for (set.items) |ch| {
        // Node ids present in this chunk (as full row or stub).
        var node_ids: std.AutoHashMapUnmanaged(storage.Id, void) = .empty;
        defer node_ids.deinit(a);
        for (ch.nodes) |n| try node_ids.put(a, n.id, {});
        var loc_ids: std.AutoHashMapUnmanaged(storage.Id, void) = .empty;
        defer loc_ids.deinit(a);
        for (ch.source_locations) |l| try loc_ids.put(a, l.id, {});

        // Self-containment: every edge endpoint is present in the same chunk.
        for (ch.edges) |e| {
            try testing.expect(node_ids.contains(e.source_node_id));
            try testing.expect(node_ids.contains(e.target_node_id));
        }
        // Every occurrence's location (and that location's file node) is present.
        for (ch.occurrences) |o| try testing.expect(loc_ids.contains(o.source_location_id));
        for (ch.source_locations) |l| try testing.expect(node_ids.contains(l.file_node_id));

        total_occurrences += ch.occurrences.len;
        total_edges += ch.edges.len;
    }
    // Every occurrence and edge is emitted exactly once across all chunks.
    try testing.expectEqual(store.occurrences.items.len, total_occurrences);
    try testing.expectEqual(store.edges.items.len, total_edges);
}
