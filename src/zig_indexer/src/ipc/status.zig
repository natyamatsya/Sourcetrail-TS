//! StatusChannel: reads/writes IndexingStatus over `ists_ipc_<uuid>`, mirroring
//! the Rust `ipc/status.rs`. The app sets `indexing_interrupted`; the indexer
//! appends its progress and, on completion, its process id to
//! `finished_process_ids` (how the app knows to drain the storage queue).
//!
//! Every mutation is a full read-modify-write that preserves all other fields
//! and other processes' entries.

const std = @import("std");
const shm = @import("shm.zig");
const c = @import("c.zig").c;

// Matches the C++ IpcInterprocessIndexingStatusManager segment size (1 MiB).
const shm_size: usize = 1048576;

pub const Error = shm.Error;

pub const StatusChannel = struct {
    shm: shm.IpcShm,
    process_id: u64,

    pub fn open(uuid: []const u8, process_id: u64) Error!StatusChannel {
        var buf: [96]u8 = undefined;
        const name = std.fmt.bufPrint(&buf, "ists_ipc_{s}", .{uuid}) catch return shm.Error.TooLarge;
        return .{ .shm = try shm.IpcShm.open(name, shm_size), .process_id = process_id };
    }

    pub fn deinit(self: *StatusChannel) void {
        self.shm.deinit();
    }

    /// True if the app has requested interruption.
    pub fn isInterrupted(self: *StatusChannel) Error!bool {
        const H = struct {
            fn run(_: void, bytes: []u8) shm.Error!bool {
                if (shm.isEmpty(bytes)) return false;
                const s = c.Sourcetrail_Ipc_IndexingStatus_as_root(bytes.ptr);
                if (s == null) return false;
                return c.Sourcetrail_Ipc_IndexingStatus_indexing_interrupted(s) != 0;
            }
        };
        return self.shm.readLocked({}, bool, H.run);
    }

    /// Report that this process is now indexing `file_path`. `current_files` is
    /// a per-process "currently working on" map (one entry per process): replace
    /// this process's entry so a *previous* file is not left dangling — a
    /// left-over `current_files` entry is reported as a CRASHED translation unit
    /// at TaskBuildIndex::doExit, which marks the file incomplete and forces it
    /// to be re-indexed every refresh.
    pub fn updateIndexing(self: *StatusChannel, gpa: std.mem.Allocator, file_path: []const u8) Error!void {
        const Ctx = struct { pid: u64, path: []const u8 };
        const H = struct {
            fn run(ctx: Ctx, a: std.mem.Allocator, bytes: []u8) shm.Error!shm.IpcShm.Outcome(void) {
                var owned = try readStatus(a, bytes);
                defer owned.deinit(a);
                removeCurrentFile(a, &owned, ctx.pid);
                try owned.indexing_file_paths.append(a, try a.dupe(u8, ctx.path));
                try owned.current_files.append(a, .{ .pid = ctx.pid, .path = try a.dupe(u8, ctx.path) });
                return .{ .write = try serializeStatus(a, &owned), .result = {} };
            }
        };
        try self.shm.readModifyWrite(gpa, Ctx{ .pid = self.process_id, .path = file_path }, void, H.run);
    }

    /// Mark the current file finished: clear this process's `current_files`
    /// entry (so it is not counted as crashed) and record it in
    /// `finished_process_ids` so the app drains this process's storage.
    pub fn finishIndexing(self: *StatusChannel, gpa: std.mem.Allocator) Error!void {
        const H = struct {
            fn run(pid: u64, a: std.mem.Allocator, bytes: []u8) shm.Error!shm.IpcShm.Outcome(void) {
                var owned = try readStatus(a, bytes);
                defer owned.deinit(a);
                removeCurrentFile(a, &owned, pid);
                try owned.finished_process_ids.append(a, pid);
                return .{ .write = try serializeStatus(a, &owned), .result = {} };
            }
        };
        try self.shm.readModifyWrite(gpa, self.process_id, void, H.run);
    }
};

const ProcFile = struct { pid: u64, path: []u8 };

const OwnedStatus = struct {
    indexing_file_paths: std.ArrayListUnmanaged([]u8) = .empty,
    current_files: std.ArrayListUnmanaged(ProcFile) = .empty,
    crashed_file_paths: std.ArrayListUnmanaged([]u8) = .empty,
    finished_process_ids: std.ArrayListUnmanaged(u64) = .empty,
    indexing_interrupted: bool = false,
    queue_stopped: bool = false,

    fn deinit(self: *OwnedStatus, a: std.mem.Allocator) void {
        for (self.indexing_file_paths.items) |p| a.free(p);
        self.indexing_file_paths.deinit(a);
        for (self.current_files.items) |f| a.free(f.path);
        self.current_files.deinit(a);
        for (self.crashed_file_paths.items) |p| a.free(p);
        self.crashed_file_paths.deinit(a);
        self.finished_process_ids.deinit(a);
    }
};

fn strSlice(s: c.flatbuffers_string_t) []const u8 {
    if (s == null) return "";
    return s[0..c.flatbuffers_string_len(s)];
}

/// Drop all `current_files` entries owned by `pid` (freeing their paths).
fn removeCurrentFile(a: std.mem.Allocator, owned: *OwnedStatus, pid: u64) void {
    var k: usize = 0;
    while (k < owned.current_files.items.len) {
        if (owned.current_files.items[k].pid == pid) {
            a.free(owned.current_files.items[k].path);
            _ = owned.current_files.swapRemove(k);
        } else {
            k += 1;
        }
    }
}

fn readStatus(a: std.mem.Allocator, bytes: []u8) shm.Error!OwnedStatus {
    var out = OwnedStatus{};
    errdefer out.deinit(a);
    if (shm.isEmpty(bytes)) return out;
    const s = c.Sourcetrail_Ipc_IndexingStatus_as_root(bytes.ptr);
    if (s == null) return out;

    const ifp = c.Sourcetrail_Ipc_IndexingStatus_indexing_file_paths(s);
    var i: usize = 0;
    const ifp_n: usize = if (ifp == null) 0 else c.flatbuffers_string_vec_len(ifp);
    while (i < ifp_n) : (i += 1) {
        try out.indexing_file_paths.append(a, try a.dupe(u8, strSlice(c.flatbuffers_string_vec_at(ifp, i))));
    }

    const cf = c.Sourcetrail_Ipc_IndexingStatus_current_files(s);
    const cf_n: usize = if (cf == null) 0 else c.Sourcetrail_Ipc_ProcessFile_vec_len(cf);
    i = 0;
    while (i < cf_n) : (i += 1) {
        const pf = c.Sourcetrail_Ipc_ProcessFile_vec_at(cf, i);
        try out.current_files.append(a, .{
            .pid = c.Sourcetrail_Ipc_ProcessFile_process_id(pf),
            .path = try a.dupe(u8, strSlice(c.Sourcetrail_Ipc_ProcessFile_file_path(pf))),
        });
    }

    const crp = c.Sourcetrail_Ipc_IndexingStatus_crashed_file_paths(s);
    const crp_n: usize = if (crp == null) 0 else c.flatbuffers_string_vec_len(crp);
    i = 0;
    while (i < crp_n) : (i += 1) {
        try out.crashed_file_paths.append(a, try a.dupe(u8, strSlice(c.flatbuffers_string_vec_at(crp, i))));
    }

    const fpi = c.Sourcetrail_Ipc_IndexingStatus_finished_process_ids(s);
    const fpi_n: usize = if (fpi == null) 0 else c.flatbuffers_uint64_vec_len(fpi);
    i = 0;
    while (i < fpi_n) : (i += 1) {
        try out.finished_process_ids.append(a, c.flatbuffers_uint64_vec_at(fpi, i));
    }

    out.indexing_interrupted = c.Sourcetrail_Ipc_IndexingStatus_indexing_interrupted(s) != 0;
    out.queue_stopped = c.Sourcetrail_Ipc_IndexingStatus_queue_stopped(s) != 0;
    return out;
}

fn serializeStatus(gpa: std.mem.Allocator, st: *const OwnedStatus) shm.Error![]u8 {
    var b: c.flatcc_builder_t = undefined;
    if (c.flatcc_builder_init(&b) != 0) return shm.Error.Shm;
    defer c.flatcc_builder_clear(&b);

    // Leaf objects (strings, ProcessFile tables) are created *before* opening
    // any vector frame — creating a nested object inside an open vec frame is a
    // flatcc frame violation (corrupts the builder). Refs are then pushed.
    const ifp_refs = try gpa.alloc(c.flatbuffers_string_ref_t, st.indexing_file_paths.items.len);
    defer gpa.free(ifp_refs);
    for (st.indexing_file_paths.items, ifp_refs) |p, *ref| ref.* = c.flatbuffers_string_create(&b, p.ptr, p.len);
    _ = c.flatbuffers_string_vec_start(&b);
    for (ifp_refs) |ref| _ = c.flatbuffers_string_vec_push(&b, ref);
    const ifp = c.flatbuffers_string_vec_end(&b);

    const pf_refs = try gpa.alloc(c.Sourcetrail_Ipc_ProcessFile_ref_t, st.current_files.items.len);
    defer gpa.free(pf_refs);
    for (st.current_files.items, pf_refs) |f, *ref| {
        ref.* = c.Sourcetrail_Ipc_ProcessFile_create(&b, f.pid, c.flatbuffers_string_create(&b, f.path.ptr, f.path.len));
    }
    _ = c.Sourcetrail_Ipc_ProcessFile_vec_start(&b);
    for (pf_refs) |ref| _ = c.Sourcetrail_Ipc_ProcessFile_vec_push(&b, ref);
    const cf = c.Sourcetrail_Ipc_ProcessFile_vec_end(&b);

    const crp_refs = try gpa.alloc(c.flatbuffers_string_ref_t, st.crashed_file_paths.items.len);
    defer gpa.free(crp_refs);
    for (st.crashed_file_paths.items, crp_refs) |p, *ref| ref.* = c.flatbuffers_string_create(&b, p.ptr, p.len);
    _ = c.flatbuffers_string_vec_start(&b);
    for (crp_refs) |ref| _ = c.flatbuffers_string_vec_push(&b, ref);
    const crp = c.flatbuffers_string_vec_end(&b);

    // Scalar (non-offset) vec push takes a POINTER to the element (flatcc
    // `_vec_push(B, const T *p)`), unlike offset/string/table pushes.
    _ = c.flatbuffers_uint64_vec_start(&b);
    for (st.finished_process_ids.items) |id| {
        var v: u64 = id;
        _ = c.flatbuffers_uint64_vec_push(&b, &v);
    }
    const fpi = c.flatbuffers_uint64_vec_end(&b);

    _ = c.Sourcetrail_Ipc_IndexingStatus_start_as_root(&b);
    _ = c.Sourcetrail_Ipc_IndexingStatus_indexing_file_paths_add(&b, ifp);
    _ = c.Sourcetrail_Ipc_IndexingStatus_current_files_add(&b, cf);
    _ = c.Sourcetrail_Ipc_IndexingStatus_crashed_file_paths_add(&b, crp);
    _ = c.Sourcetrail_Ipc_IndexingStatus_finished_process_ids_add(&b, fpi);
    _ = c.Sourcetrail_Ipc_IndexingStatus_indexing_interrupted_add(&b, if (st.indexing_interrupted) 1 else 0);
    _ = c.Sourcetrail_Ipc_IndexingStatus_queue_stopped_add(&b, if (st.queue_stopped) 1 else 0);
    _ = c.Sourcetrail_Ipc_IndexingStatus_end_as_root(&b);

    var size: usize = 0;
    const raw = c.flatcc_builder_finalize_aligned_buffer(&b, &size) orelse return shm.Error.Shm;
    defer c.flatcc_builder_aligned_free(raw);
    const out = try gpa.alloc(u8, size);
    @memcpy(out, @as([*]const u8, @ptrCast(raw))[0..size]);
    return out;
}
