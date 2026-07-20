//! CommandChannel: reads Zig IndexerCommands from `icmd_ipc_<uuid>` (64 MiB)
//! and writes the queue back minus the popped command, mirroring the Rust
//! `ipc/command.rs`.
//!
//! Popping is a single read-modify-write under the mutex: find the first
//! `Zig`-typed command, copy its fields out, and re-serialize the queue with
//! every *other* command deep-cloned (flatcc `_clone`) so all fields of every
//! remaining command — including other languages' — survive untouched.

const std = @import("std");
const shm = @import("shm.zig");
const c = @import("c.zig").c;

const shm_size: usize = 64 * 1024 * 1024;

pub const Error = shm.Error;

/// A popped Zig command, owned by the caller (strings duped out of the SHM).
pub const Command = struct {
    source_file_path: []u8,
    working_directory: []u8,
    indexed_paths: [][]u8,

    pub fn deinit(self: *Command, gpa: std.mem.Allocator) void {
        gpa.free(self.source_file_path);
        gpa.free(self.working_directory);
        for (self.indexed_paths) |p| gpa.free(p);
        gpa.free(self.indexed_paths);
    }
};

pub const CommandChannel = struct {
    shm: shm.IpcShm,

    pub fn open(uuid: []const u8) Error!CommandChannel {
        var buf: [96]u8 = undefined;
        const name = std.fmt.bufPrint(&buf, "icmd_ipc_{s}", .{uuid}) catch return shm.Error.TooLarge;
        return .{ .shm = try shm.IpcShm.open(name, shm_size) };
    }

    pub fn deinit(self: *CommandChannel) void {
        self.shm.deinit();
    }

    /// Pop the first Zig command, rewriting the queue without it. Returns null
    /// when no Zig command remains (and leaves the queue untouched).
    pub fn popZig(self: *CommandChannel, gpa: std.mem.Allocator) Error!?Command {
        return self.shm.readModifyWrite(gpa, {}, ?Command, popFromBytes);
    }
};

fn strSlice(s: c.flatbuffers_string_t) []const u8 {
    if (s == null) return "";
    return s[0..c.flatbuffers_string_len(s)];
}

fn popFromBytes(_: void, gpa: std.mem.Allocator, bytes: []u8) shm.Error!shm.IpcShm.Outcome(?Command) {
    const none = shm.IpcShm.Outcome(?Command){ .write = null, .result = null };
    if (shm.isEmpty(bytes)) return none;

    const queue = c.Sourcetrail_Ipc_IndexerCommandQueue_as_root(bytes.ptr);
    if (queue == null) return none;
    const cmds = c.Sourcetrail_Ipc_IndexerCommandQueue_commands(queue);
    const len = c.Sourcetrail_Ipc_IndexerCommand_vec_len(cmds);

    // Locate the first Zig command.
    var zig_index: ?usize = null;
    var i: usize = 0;
    while (i < len) : (i += 1) {
        const cmd = c.Sourcetrail_Ipc_IndexerCommand_vec_at(cmds, i);
        if (c.Sourcetrail_Ipc_IndexerCommand_type(cmd) == c.Sourcetrail_Ipc_IndexerCommandType_Zig) {
            zig_index = i;
            break;
        }
    }
    const idx = zig_index orelse return none;

    // Copy the popped command's fields out of the SHM.
    const popped = try copyCommand(gpa, c.Sourcetrail_Ipc_IndexerCommand_vec_at(cmds, idx));

    // Re-serialize the queue with every other command cloned verbatim.
    const rebuilt = rebuildWithout(gpa, cmds, len, idx) catch |err| {
        var p = popped;
        p.deinit(gpa);
        return err;
    };
    return .{ .write = rebuilt, .result = popped };
}

fn copyCommand(gpa: std.mem.Allocator, cmd: c.Sourcetrail_Ipc_IndexerCommand_table_t) shm.Error!Command {
    const src = try gpa.dupe(u8, strSlice(c.Sourcetrail_Ipc_IndexerCommand_source_file_path(cmd)));
    errdefer gpa.free(src);
    const wd = try gpa.dupe(u8, strSlice(c.Sourcetrail_Ipc_IndexerCommand_working_directory(cmd)));
    errdefer gpa.free(wd);

    const paths_vec = c.Sourcetrail_Ipc_IndexerCommand_indexed_paths(cmd);
    const n: usize = if (paths_vec == null) 0 else c.flatbuffers_string_vec_len(paths_vec);
    const paths = try gpa.alloc([]u8, n);
    errdefer gpa.free(paths);
    var filled: usize = 0;
    errdefer for (paths[0..filled]) |p| gpa.free(p);
    while (filled < n) : (filled += 1) {
        paths[filled] = try gpa.dupe(u8, strSlice(c.flatbuffers_string_vec_at(paths_vec, filled)));
    }

    return .{ .source_file_path = src, .working_directory = wd, .indexed_paths = paths };
}

fn rebuildWithout(gpa: std.mem.Allocator, cmds: c.Sourcetrail_Ipc_IndexerCommand_vec_t, len: usize, skip: usize) shm.Error![]u8 {
    var b: c.flatcc_builder_t = undefined;
    if (c.flatcc_builder_init(&b) != 0) return shm.Error.Shm;
    defer c.flatcc_builder_clear(&b);

    _ = c.Sourcetrail_Ipc_IndexerCommandQueue_start_as_root(&b);
    _ = c.Sourcetrail_Ipc_IndexerCommand_vec_start(&b);
    var i: usize = 0;
    while (i < len) : (i += 1) {
        if (i == skip) continue;
        const ref = c.Sourcetrail_Ipc_IndexerCommand_clone(&b, c.Sourcetrail_Ipc_IndexerCommand_vec_at(cmds, i));
        _ = c.Sourcetrail_Ipc_IndexerCommand_vec_push(&b, ref);
    }
    const vec = c.Sourcetrail_Ipc_IndexerCommand_vec_end(&b);
    _ = c.Sourcetrail_Ipc_IndexerCommandQueue_commands_add(&b, vec);
    _ = c.Sourcetrail_Ipc_IndexerCommandQueue_end_as_root(&b);

    var size: usize = 0;
    const raw = c.flatcc_builder_finalize_aligned_buffer(&b, &size) orelse return shm.Error.Shm;
    defer c.flatcc_builder_aligned_free(raw);
    const out = try gpa.alloc(u8, size);
    @memcpy(out, @as([*]const u8, @ptrCast(raw))[0..size]);
    return out;
}
