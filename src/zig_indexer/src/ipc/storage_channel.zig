//! StorageChannel: pushes IntermediateStorage results back to the app over
//! `iist_ipc_<processId>_<uuid>` (16 MiB), mirroring the Rust `ipc/storage.rs`.

const std = @import("std");
const indexer = @import("indexer");
const shm = @import("shm.zig");
const queue = @import("queue.zig");
const wire = @import("wire.zig");

const shm_size: usize = 16 * 1024 * 1024;

pub const Error = shm.Error || wire.Error;

pub const StorageChannel = struct {
    shm: shm.IpcShm,

    pub fn open(uuid: []const u8, process_id: u64) Error!StorageChannel {
        var buf: [96]u8 = undefined;
        const name = std.fmt.bufPrint(&buf, "iist_ipc_{d}_{s}", .{ process_id, uuid }) catch return shm.Error.TooLarge;
        return .{ .shm = try shm.IpcShm.open(name, shm_size) };
    }

    pub fn deinit(self: *StorageChannel) void {
        self.shm.deinit();
    }

    /// Number of storage entries currently queued (back-pressure guard).
    pub fn count(self: *StorageChannel) Error!usize {
        const H = struct {
            fn run(_: void, bytes: []u8) shm.Error!usize {
                return queue.readCount(bytes);
            }
        };
        return self.shm.readLocked({}, usize, H.run);
    }

    /// Serialize one `Chunk` and append it as one entry to the SHM queue. Large
    /// stores are split into several budget-sized chunks by the chunker first
    /// (see `indexer.chunker`), so a single entry never exceeds the segment.
    pub fn push(self: *StorageChannel, gpa: std.mem.Allocator, chunk: *const indexer.Chunk) Error!void {
        const entry = try wire.serializeChunk(gpa, chunk);
        defer gpa.free(entry);

        const Appender = struct {
            fn run(e: []const u8, a: std.mem.Allocator, bytes: []u8) shm.Error!shm.IpcShm.Outcome(void) {
                const new = queue.appendEntry(a, bytes, e, shm_size) catch |err| return switch (err) {
                    error.OutOfMemory => shm.Error.OutOfMemory,
                    error.SegmentTooSmall, error.EntryTooLarge => shm.Error.TooLarge,
                    else => shm.Error.Shm,
                };
                return .{ .write = new, .result = {} };
            }
        };
        try self.shm.readModifyWrite(gpa, entry, void, Appender.run);
    }
};
