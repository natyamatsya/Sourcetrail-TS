const std = @import("std");
const testing = std.testing;
const indexer = @import("indexer");
const storage = indexer.storage;
const NodeKind = storage.NodeKind;
const EdgeType = storage.EdgeType;

fn indexString(gpa: std.mem.Allocator, store: *storage.Storage, src: [:0]const u8) !void {
    try indexer.parser.indexSource(gpa, store, "test.zig", src);
}

/// Find the first node with the given serialized name; null if absent.
/// Match by the file-local name: exact (file nodes) or the `<file>::<local>`
/// suffix (symbol nodes are file-qualified via storage.qualifiedName).
fn nodeNamed(store: *storage.Storage, local: []const u8) ?storage.StorageNode {
    for (store.nodes.items) |n| {
        if (std.mem.eql(u8, n.serialized_name, local)) return n;
        if (std.mem.endsWith(u8, n.serialized_name, "::") == false and
            n.serialized_name.len > local.len + 2 and
            std.mem.endsWith(u8, n.serialized_name, local) and
            std.mem.endsWith(u8, n.serialized_name[0 .. n.serialized_name.len - local.len], "::")) return n;
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

test "@import produces an EDGE_IMPORT to a file node" {
    var store = storage.Storage.init(testing.allocator);
    defer store.deinit();
    try indexString(testing.allocator, &store,
        \\const std = @import("std");
        \\const util = @import("util.zig");
    );
    try testing.expectEqual(@as(usize, 2), countEdges(&store, .import));
    // The imported paths are recorded as (non-indexed) file nodes.
    try testing.expect(nodeNamed(&store, "std") != null);
    try testing.expect(nodeNamed(&store, "util.zig") != null);
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
