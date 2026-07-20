//! The intermediate-storage SHM queue framing — byte-for-byte compatible with
//! the C++ IpcInterprocessIntermediateStorageManager and the Rust
//! `ipc/storage.rs`.
//!
//! Layout: `[u64 needed_capacity][u32 count][u32 size0][bytes0][u32 size1]...`
//! All little-endian. The `needed_capacity` field is preserved for wire
//! compatibility; the fixed-size segment (ADR-0002) never grows, so oversized
//! payloads must be chunked by the writer before appending.

const std = @import("std");

pub const cap_field_size: usize = @sizeOf(u64);
pub const count_field_size: usize = @sizeOf(u32);
pub const header_size: usize = cap_field_size + count_field_size;

pub const Error = error{ TruncatedEntry, SizeOverflow, EntryTooLarge, SegmentTooSmall, OutOfMemory };

fn readU32(data: []const u8, offset: usize) ?u32 {
    if (offset + 4 > data.len) return null;
    return std.mem.readInt(u32, data[offset..][0..4], .little);
}

fn readU64(data: []const u8, offset: usize) ?u64 {
    if (offset + 8 > data.len) return null;
    return std.mem.readInt(u64, data[offset..][0..8], .little);
}

fn writeU32(data: []u8, offset: usize, value: u32) void {
    std.mem.writeInt(u32, data[offset..][0..4], value, .little);
}

fn writeU64(data: []u8, offset: usize, value: u64) void {
    std.mem.writeInt(u64, data[offset..][0..8], value, .little);
}

/// Number of storage entries currently in the queue (0 for an empty/blank slot).
pub fn readCount(data: []const u8) u32 {
    return readU32(data, cap_field_size) orelse 0;
}

/// Byte offset just past the last entry's payload (walks the length-prefixed
/// entries). Errors on a truncated/overflowing queue.
pub fn payloadEnd(data: []const u8, count: u32) Error!usize {
    var cursor: usize = header_size;
    var i: u32 = 0;
    while (i < count) : (i += 1) {
        const size = readU32(data, cursor) orelse return Error.TruncatedEntry;
        cursor = std.math.add(usize, cursor, count_field_size) catch return Error.SizeOverflow;
        cursor = std.math.add(usize, cursor, size) catch return Error.SizeOverflow;
        if (cursor > data.len) return Error.SizeOverflow;
    }
    return cursor;
}

/// Total bytes the queue would occupy after appending an `entry_size`-byte entry.
pub fn requiredSizeForAppend(queue: []const u8, entry_size: usize) Error!usize {
    const end = try payloadEnd(queue, readCount(queue));
    const existing_payload = end -| header_size;
    var total = std.math.add(usize, header_size, existing_payload) catch return Error.SizeOverflow;
    total = std.math.add(usize, total, count_field_size) catch return Error.SizeOverflow;
    total = std.math.add(usize, total, entry_size) catch return Error.SizeOverflow;
    return total;
}

/// Return a freshly-allocated queue equal to `queue` with `entry` appended.
/// Caller owns the returned slice. Fails with `SegmentTooSmall` if the result
/// would exceed `segment_size` (the fixed SHM user size).
pub fn appendEntry(gpa: std.mem.Allocator, queue: []const u8, entry: []const u8, segment_size: usize) Error![]u8 {
    const count = readCount(queue);
    const end = try payloadEnd(queue, count);
    const existing_payload = end -| header_size;

    if (entry.len > std.math.maxInt(u32)) return Error.EntryTooLarge;
    const new_count = std.math.add(u32, count, 1) catch return Error.SizeOverflow;
    const total = try requiredSizeForAppend(queue, entry.len);
    if (total > segment_size) return Error.SegmentTooSmall;

    const out = try gpa.alloc(u8, total);
    @memset(out, 0);
    // Preserve the needed_capacity field verbatim.
    writeU64(out, 0, readU64(queue, 0) orelse 0);
    writeU32(out, cap_field_size, new_count);
    if (existing_payload > 0) {
        @memcpy(out[header_size .. header_size + existing_payload], queue[header_size..end]);
    }
    const size_off = header_size + existing_payload;
    writeU32(out, size_off, @intCast(entry.len));
    const payload_off = size_off + count_field_size;
    @memcpy(out[payload_off .. payload_off + entry.len], entry);
    return out;
}

/// A blank (empty) queue slot: the first 4 bytes zero mark it empty for readers.
pub const empty_marker = [_]u8{0} ** 4;

// ---------------------------------------------------------------------------

test "readCount on empty/blank slot is 0" {
    try std.testing.expectEqual(@as(u32, 0), readCount(&empty_marker));
    try std.testing.expectEqual(@as(u32, 0), readCount(&[_]u8{}));
}

test "append then walk: count, framing, payload round-trip" {
    const a = std.testing.allocator;
    // Start from a blank slot (header zeroed, count 0).
    var q: []u8 = try a.dupe(u8, &([_]u8{0} ** header_size));
    defer a.free(q);

    const e0 = "hello";
    const e1 = "world!!";
    const seg = 4096;

    const q1 = try appendEntry(a, q, e0, seg);
    a.free(q);
    q = q1;
    try std.testing.expectEqual(@as(u32, 1), readCount(q));

    const q2 = try appendEntry(a, q, e1, seg);
    a.free(q);
    q = q2;
    try std.testing.expectEqual(@as(u32, 2), readCount(q));

    // Walk the two entries back out.
    var cursor: usize = header_size;
    const s0 = readU32(q, cursor).?;
    try std.testing.expectEqual(@as(u32, e0.len), s0);
    cursor += count_field_size;
    try std.testing.expectEqualStrings(e0, q[cursor .. cursor + s0]);
    cursor += s0;
    const s1 = readU32(q, cursor).?;
    try std.testing.expectEqual(@as(u32, e1.len), s1);
    cursor += count_field_size;
    try std.testing.expectEqualStrings(e1, q[cursor .. cursor + s1]);

    try std.testing.expectEqual(cursor + s1, try payloadEnd(q, 2));
}

test "append fails when it would exceed the fixed segment" {
    const a = std.testing.allocator;
    const q = [_]u8{0} ** header_size;
    try std.testing.expectError(Error.SegmentTooSmall, appendEntry(a, &q, "toolong", header_size + 4 + 3));
}
