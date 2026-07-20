//! Unit tests for the ZLS semantic pass (semantic.zig). Unlike the pure-core
//! tests, these spin up a real ZLS `Session` (via `std.testing.io`, the same io
//! ZLS's own tests use) over an in-memory document and assert the resolved
//! typedef reclassification, call edge, type-usage edge, and local symbol — the
//! path otherwise covered only by end-to-end indexing.

const std = @import("std");
const testing = std.testing;
const indexer = @import("indexer");
const semantic = @import("semantic.zig");
const storage = indexer.storage;

fn hasEdge(store: *storage.Storage, kind: storage.EdgeType, target: storage.Id) bool {
    for (store.edges.items) |e| {
        if (e.kind == kind and e.target_node_id == target) return true;
    }
    return false;
}

/// Node id for the symbol declared at `local` (dotted) in `path`.
fn nodeId(store: *storage.Storage, a: std.mem.Allocator, path: []const u8, local: []const u8) storage.Id {
    const name = storage.qualifiedName(a, path, local) catch unreachable;
    defer a.free(name);
    return store.node_by_name.get(name).?;
}

test "semantic pass resolves typedefs, method calls, type usage and locals" {
    const a = testing.allocator;
    const io = std.testing.io;
    // An absolute path with no build.zig above it: ZLS keeps the source in
    // memory (LSP-synced) so the test touches no real files.
    const path = "/nonexistent-zig-sem-test/main.zig";
    const source: [:0]const u8 =
        \\const Point = struct {
        \\    x: i32,
        \\    pub fn add(self: Point, o: Point) Point { return .{ .x = self.x + o.x }; }
        \\};
        \\const Alias = Point;
        \\pub fn use() void {
        \\    const p = Point{ .x = 1 };
        \\    _ = p.add(p);
        \\}
    ;

    var store = storage.Storage.init(a);
    defer store.deinit();
    try indexer.parser.indexSource(a, &store, path, source);

    var session = try semantic.Session.init(a, io, null);
    defer session.deinit();
    const handle = try session.openDocument(path, source);
    const file_id = store.node_by_name.get(path).?;
    try semantic.resolveTypedefs(&session, &store, handle, path);
    try semantic.resolveReferences(&session, &store, handle, path, file_id);

    // `const Alias = Point;` is upgraded from global_variable to typedef.
    try testing.expectEqual(storage.NodeKind.typedef, store.nodeKind(nodeId(&store, a, path, "Alias")).?);

    // `p.add(p)` -> EDGE_CALL to the nested method Point.add.
    try testing.expect(hasEdge(&store, .call, nodeId(&store, a, path, "Point.add")));

    // `Point{ … }` and the `Point` params/return -> EDGE_TYPE_USAGE to Point.
    try testing.expect(hasEdge(&store, .type_usage, nodeId(&store, a, path, "Point")));

    // The local binding `p` is recorded as a local symbol.
    try testing.expect(store.local_symbols.items.len > 0);
}
