//! Raw shared-memory + named-mutex channel, mirroring the C++ IpcSharedMemory
//! and the Rust `ipc/shm.rs`. A segment holds raw FlatBuffers bytes directly
//! ("empty" = first 4 bytes zero); a named mutex serialises all access.
//!
//! Names pass through at full length; thoth-ipc hashes any name over the OS
//! limit identically on every frontend, so the Zig indexer opens the exact
//! object the C++ core created.

const std = @import("std");
const thoth = @import("thoth-ipc");

const mem_prefix = "srctrl_ipc_mem_";
const mtx_prefix = "srctrl_ipc_mtx_";

pub const Error = error{ Timeout, TooLarge, Shm, OutOfMemory };

pub const IpcShm = struct {
    handle: thoth.ShmHandle,
    mutex: thoth.Mutex,

    pub fn open(name: []const u8, size: usize) Error!IpcShm {
        var mem_buf: [128]u8 = undefined;
        var mtx_buf: [128]u8 = undefined;
        const mem_name = std.fmt.bufPrint(&mem_buf, "{s}{s}", .{ mem_prefix, name }) catch return Error.TooLarge;
        const mtx_name = std.fmt.bufPrint(&mtx_buf, "{s}{s}", .{ mtx_prefix, name }) catch return Error.TooLarge;

        const handle = thoth.ShmHandle.acquire(mem_name, size, .create_or_open) catch return Error.Shm;
        const mutex = thoth.Mutex.open(mtx_name) catch return Error.Shm;
        return .{ .handle = handle, .mutex = mutex };
    }

    pub fn deinit(self: *IpcShm) void {
        self.mutex.deinit();
        self.handle.release();
    }

    fn userBytes(self: *IpcShm) []u8 {
        return self.handle.ptr()[0..self.handle.user_size];
    }

    fn lock(self: *IpcShm) Error!void {
        const timeout_ms: i128 = 500;
        var attempt: u32 = 0;
        while (attempt < 60) : (attempt += 1) {
            if (self.mutex.lockTimeout(timeout_ms * std.time.ns_per_ms)) return;
        }
        return Error.Timeout;
    }

    /// Run `f(ctx, bytes)` with the raw SHM bytes held under the mutex.
    pub fn readLocked(self: *IpcShm, ctx: anytype, comptime R: type, f: anytype) Error!R {
        try self.lock();
        defer self.mutex.unlock();
        return f(ctx, self.userBytes());
    }

    /// Overwrite the segment with `buf` under the mutex.
    pub fn writeLocked(self: *IpcShm, buf: []const u8) Error!void {
        try self.lock();
        defer self.mutex.unlock();
        const dst = self.userBytes();
        if (buf.len > dst.len) return Error.TooLarge;
        @memcpy(dst[0..buf.len], buf);
    }

    /// Mark the segment empty (first 4 bytes zero).
    pub fn clearLocked(self: *IpcShm) Error!void {
        try self.writeLocked(&[_]u8{0} ** 4);
    }

    /// The result of a read-modify-write step: optional replacement bytes to
    /// write back (allocated with the RMW's gpa; freed after) and a result.
    pub fn Outcome(comptime R: type) type {
        return struct { write: ?[]u8, result: R };
    }

    /// Read current bytes, let `f` optionally produce replacement bytes plus a
    /// result — all under one lock. `f` has signature
    /// `fn(ctx, gpa, bytes) Error!Outcome(R)`; a returned buffer is written back
    /// and then freed before unlocking.
    pub fn readModifyWrite(self: *IpcShm, gpa: std.mem.Allocator, ctx: anytype, comptime R: type, f: anytype) Error!R {
        try self.lock();
        defer self.mutex.unlock();
        const dst = self.userBytes();
        const outcome = try f(ctx, gpa, dst);
        if (outcome.write) |buf| {
            defer gpa.free(buf);
            if (buf.len > dst.len) return Error.TooLarge;
            @memcpy(dst[0..buf.len], buf);
        }
        return outcome.result;
    }
};

/// True when the raw bytes represent an "empty" slot.
pub fn isEmpty(data: []const u8) bool {
    return data.len < 4 or std.mem.eql(u8, data[0..4], &[_]u8{ 0, 0, 0, 0 });
}
