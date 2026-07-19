const std = @import("std");
const testing = std.testing;
const indexer = @import("indexer");
const storage = indexer.storage;
const wire = @import("wire.zig");
const c = @import("c.zig").c;

// Pull in the pure framing tests.
test {
    _ = @import("queue.zig");
    _ = @import("status.zig");
}

test "IntermediateStorage FlatBuffers round-trip (build -> read back)" {
    const a = testing.allocator;
    var store = storage.Storage.init(a);
    defer store.deinit();

    // Populate via the real parser so the shapes match production output.
    try indexer.indexSource(a, &store, "test.zig",
        \\const Point = struct {
        \\    x: i32,
        \\    pub fn norm(self: Point) i32 { return self.x; }
        \\};
        \\const std_mod = @import("std");
    );

    const whole = store.wholeChunk();
    const buf = try wire.serializeChunk(a, &whole);
    defer a.free(buf);

    // Verify the buffer with the generated verifier before reading.
    try testing.expectEqual(@as(c_int, 0), c.Sourcetrail_Ipc_IntermediateStorageQueue_verify_as_root(buf.ptr, buf.len));

    const queue = c.Sourcetrail_Ipc_IntermediateStorageQueue_as_root(buf.ptr);
    try testing.expect(queue != null);
    const storages = c.Sourcetrail_Ipc_IntermediateStorageQueue_storages(queue);
    try testing.expectEqual(@as(usize, 1), c.Sourcetrail_Ipc_IntermediateStorage_vec_len(storages));

    const s = c.Sourcetrail_Ipc_IntermediateStorage_vec_at(storages, 0);

    // Counts round-trip exactly.
    const nodes = c.Sourcetrail_Ipc_IntermediateStorage_nodes(s);
    const edges = c.Sourcetrail_Ipc_IntermediateStorage_edges(s);
    const locs = c.Sourcetrail_Ipc_IntermediateStorage_source_locations(s);
    try testing.expectEqual(store.nodes.items.len, c.Sourcetrail_Ipc_StorageNode_vec_len(nodes));
    try testing.expectEqual(store.edges.items.len, c.Sourcetrail_Ipc_StorageEdge_vec_len(edges));
    try testing.expectEqual(store.source_locations.items.len, c.Sourcetrail_Ipc_StorageSourceLocation_vec_len(locs));
    try testing.expectEqual(store.next_id, c.Sourcetrail_Ipc_IntermediateStorage_next_id(s));

    // A specific node survives with its serialized name + kind.
    var found_point = false;
    var i: usize = 0;
    const n = c.Sourcetrail_Ipc_StorageNode_vec_len(nodes);
    while (i < n) : (i += 1) {
        const node = c.Sourcetrail_Ipc_StorageNode_vec_at(nodes, i);
        const name = c.Sourcetrail_Ipc_StorageNode_serialized_name(node);
        const name_slice = name[0..c.flatbuffers_string_len(name)];
        if (std.mem.endsWith(u8, name_slice, "::Point")) {
            found_point = true;
            try testing.expectEqual(@intFromEnum(storage.NodeKind.@"struct"), c.Sourcetrail_Ipc_StorageNode_type(node));
        }
    }
    try testing.expect(found_point);
}
