//! Serialize an owned `Storage` into an `IntermediateStorageQueue` FlatBuffer
//! (one storage entry), byte-compatible with the C++ IntermediateStorage
//! deserializer. Built with the flatcc builder runtime (see src/c.zig).
//!
//! flatcc frame rule: leaf objects (strings, element tables) are created
//! atomically *before* the vector that holds them is opened; vectors and the
//! enclosing table nest in strict LIFO order under the queue root.

const std = @import("std");
const indexer = @import("indexer");
const storage = indexer.storage;
const c = @import("c.zig").c;

pub const Error = error{ BuildFailed, OutOfMemory };

fn str(b: *c.flatcc_builder_t, s: []const u8) c.flatbuffers_string_ref_t {
    return c.flatbuffers_string_create(b, s.ptr, s.len);
}

/// Serialize one `Chunk` into a queue buffer holding a single
/// `IntermediateStorage`. Caller owns the returned slice (allocated with `gpa`).
pub fn serializeChunk(gpa: std.mem.Allocator, chunk: *const storage.Chunk) Error![]u8 {
    var b: c.flatcc_builder_t = undefined;
    if (c.flatcc_builder_init(&b) != 0) return Error.BuildFailed;
    defer c.flatcc_builder_clear(&b);

    _ = c.Sourcetrail_Ipc_IntermediateStorageQueue_start_as_root(&b);

    const storage_ref = try buildStorage(&b, chunk);

    _ = c.Sourcetrail_Ipc_IntermediateStorage_vec_start(&b);
    _ = c.Sourcetrail_Ipc_IntermediateStorage_vec_push(&b, storage_ref);
    const storages = c.Sourcetrail_Ipc_IntermediateStorage_vec_end(&b);
    _ = c.Sourcetrail_Ipc_IntermediateStorageQueue_storages_add(&b, storages);

    _ = c.Sourcetrail_Ipc_IntermediateStorageQueue_end_as_root(&b);

    var size: usize = 0;
    const raw = c.flatcc_builder_finalize_aligned_buffer(&b, &size) orelse return Error.BuildFailed;
    defer c.flatcc_builder_aligned_free(raw);

    const out = try gpa.alloc(u8, size);
    @memcpy(out, @as([*]const u8, @ptrCast(raw))[0..size]);
    return out;
}

fn buildStorage(b: *c.flatcc_builder_t, chunk: *const storage.Chunk) Error!c.Sourcetrail_Ipc_IntermediateStorage_ref_t {
    var arena_state = std.heap.ArenaAllocator.init(std.heap.c_allocator);
    defer arena_state.deinit();
    const a = arena_state.allocator();

    // --- nodes ---
    const node_refs = try a.alloc(c.Sourcetrail_Ipc_StorageNode_ref_t, chunk.nodes.len);
    for (chunk.nodes, node_refs) |n, *ref| {
        ref.* = c.Sourcetrail_Ipc_StorageNode_create(b, n.id, @intFromEnum(n.kind), str(b, n.serialized_name), n.modifiers);
    }
    _ = c.Sourcetrail_Ipc_StorageNode_vec_start(b);
    for (node_refs) |ref| _ = c.Sourcetrail_Ipc_StorageNode_vec_push(b, ref);
    const nodes_vec = c.Sourcetrail_Ipc_StorageNode_vec_end(b);

    // --- files ---
    const file_refs = try a.alloc(c.Sourcetrail_Ipc_StorageFile_ref_t, chunk.files.len);
    for (chunk.files, file_refs) |f, *ref| {
        ref.* = c.Sourcetrail_Ipc_StorageFile_create(b, f.id, str(b, f.file_path), str(b, f.language_identifier), boolFb(f.indexed), boolFb(f.complete));
    }
    _ = c.Sourcetrail_Ipc_StorageFile_vec_start(b);
    for (file_refs) |ref| _ = c.Sourcetrail_Ipc_StorageFile_vec_push(b, ref);
    const files_vec = c.Sourcetrail_Ipc_StorageFile_vec_end(b);

    // --- edges ---
    _ = c.Sourcetrail_Ipc_StorageEdge_vec_start(b);
    for (chunk.edges) |e| {
        _ = c.Sourcetrail_Ipc_StorageEdge_vec_push(b, c.Sourcetrail_Ipc_StorageEdge_create(b, e.id, @intFromEnum(e.kind), e.source_node_id, e.target_node_id));
    }
    const edges_vec = c.Sourcetrail_Ipc_StorageEdge_vec_end(b);

    // --- symbols ---
    _ = c.Sourcetrail_Ipc_StorageSymbol_vec_start(b);
    for (chunk.symbols) |s| {
        _ = c.Sourcetrail_Ipc_StorageSymbol_vec_push(b, c.Sourcetrail_Ipc_StorageSymbol_create(b, s.id, @intFromEnum(s.definition_kind)));
    }
    const symbols_vec = c.Sourcetrail_Ipc_StorageSymbol_vec_end(b);

    // --- source locations ---
    _ = c.Sourcetrail_Ipc_StorageSourceLocation_vec_start(b);
    for (chunk.source_locations) |l| {
        _ = c.Sourcetrail_Ipc_StorageSourceLocation_vec_push(b, c.Sourcetrail_Ipc_StorageSourceLocation_create(b, l.id, l.file_node_id, l.start_line, l.start_col, l.end_line, l.end_col, @intFromEnum(l.kind)));
    }
    const locations_vec = c.Sourcetrail_Ipc_StorageSourceLocation_vec_end(b);

    // --- local symbols ---
    const local_refs = try a.alloc(c.Sourcetrail_Ipc_StorageLocalSymbol_ref_t, chunk.local_symbols.len);
    for (chunk.local_symbols, local_refs) |ls, *ref| {
        ref.* = c.Sourcetrail_Ipc_StorageLocalSymbol_create(b, ls.id, str(b, ls.name));
    }
    _ = c.Sourcetrail_Ipc_StorageLocalSymbol_vec_start(b);
    for (local_refs) |ref| _ = c.Sourcetrail_Ipc_StorageLocalSymbol_vec_push(b, ref);
    const local_symbols_vec = c.Sourcetrail_Ipc_StorageLocalSymbol_vec_end(b);

    // --- occurrences ---
    _ = c.Sourcetrail_Ipc_StorageOccurrence_vec_start(b);
    for (chunk.occurrences) |o| {
        _ = c.Sourcetrail_Ipc_StorageOccurrence_vec_push(b, c.Sourcetrail_Ipc_StorageOccurrence_create(b, o.element_id, o.source_location_id));
    }
    const occurrences_vec = c.Sourcetrail_Ipc_StorageOccurrence_vec_end(b);

    // --- component accesses ---
    _ = c.Sourcetrail_Ipc_StorageComponentAccess_vec_start(b);
    for (chunk.component_accesses) |ca| {
        _ = c.Sourcetrail_Ipc_StorageComponentAccess_vec_push(b, c.Sourcetrail_Ipc_StorageComponentAccess_create(b, ca.node_id, @intFromEnum(ca.access)));
    }
    const component_accesses_vec = c.Sourcetrail_Ipc_StorageComponentAccess_vec_end(b);

    // --- errors ---
    const error_refs = try a.alloc(c.Sourcetrail_Ipc_StorageError_ref_t, chunk.errors.len);
    for (chunk.errors, error_refs) |er, *ref| {
        ref.* = c.Sourcetrail_Ipc_StorageError_create(b, er.id, str(b, er.message), str(b, er.translation_unit), boolFb(er.fatal), boolFb(er.indexed));
    }
    _ = c.Sourcetrail_Ipc_StorageError_vec_start(b);
    for (error_refs) |ref| _ = c.Sourcetrail_Ipc_StorageError_vec_push(b, ref);
    const errors_vec = c.Sourcetrail_Ipc_StorageError_vec_end(b);

    // --- assemble the storage table ---
    _ = c.Sourcetrail_Ipc_IntermediateStorage_start(b);
    _ = c.Sourcetrail_Ipc_IntermediateStorage_next_id_add(b, chunk.next_id);
    _ = c.Sourcetrail_Ipc_IntermediateStorage_nodes_add(b, nodes_vec);
    _ = c.Sourcetrail_Ipc_IntermediateStorage_files_add(b, files_vec);
    _ = c.Sourcetrail_Ipc_IntermediateStorage_edges_add(b, edges_vec);
    _ = c.Sourcetrail_Ipc_IntermediateStorage_symbols_add(b, symbols_vec);
    _ = c.Sourcetrail_Ipc_IntermediateStorage_source_locations_add(b, locations_vec);
    _ = c.Sourcetrail_Ipc_IntermediateStorage_local_symbols_add(b, local_symbols_vec);
    _ = c.Sourcetrail_Ipc_IntermediateStorage_occurrences_add(b, occurrences_vec);
    _ = c.Sourcetrail_Ipc_IntermediateStorage_component_accesses_add(b, component_accesses_vec);
    _ = c.Sourcetrail_Ipc_IntermediateStorage_errors_add(b, errors_vec);
    return c.Sourcetrail_Ipc_IntermediateStorage_end(b);
}

inline fn boolFb(v: bool) c.flatbuffers_bool_t {
    return if (v) 1 else 0;
}
