//! `sourcetrail_zig_indexer` entry point.
//!
//! Phase 2 will parse the positional argv `<processId> <instanceUuid> <appPath>
//! <userDataPath> <logFilePath>`, open the thoth-ipc channels, and run the
//! pop-command → index → push-storage loop (see ROADMAP_ZIG_INDEXER.md).
//!
//! For this Phase-3a milestone `main` is a standalone driver: given one or more
//! .zig file paths, it parses each and prints a summary of extracted symbols —
//! enough to smoke-test the parser end-to-end without the IPC layer.

const std = @import("std");
const indexer = @import("indexer");

pub fn main(init: std.process.Init) !void {
    const gpa = init.gpa;

    var args = try init.minimal.args.iterateAllocator(gpa);
    defer args.deinit();
    const prog = args.next() orelse "sourcetrail_zig_indexer";

    var stdout_buf: [4096]u8 = undefined;
    var stdout_writer = std.Io.File.stdout().writer(init.io, &stdout_buf);
    const out = &stdout_writer.interface;

    var any = false;
    while (args.next()) |path| {
        any = true;
        try indexOne(init.io, gpa, out, path);
    }
    if (!any) try out.print("usage: {s} <file.zig> [more.zig ...]\n", .{prog});
    try out.flush();
}

fn indexOne(io: std.Io, gpa: std.mem.Allocator, out: *std.Io.Writer, path: []const u8) !void {
    const source = std.Io.Dir.cwd().readFileAllocOptions(
        io,
        path,
        gpa,
        .limited(16 * 1024 * 1024),
        .of(u8),
        0,
    ) catch |err| {
        try out.print("skip {s}: {s}\n", .{ path, @errorName(err) });
        return;
    };
    defer gpa.free(source);

    var store = indexer.Storage.init(gpa);
    defer store.deinit();
    try indexer.indexSource(gpa, &store, path, source);

    try out.print(
        "{s}: {d} nodes, {d} edges, {d} locations, {d} errors\n",
        .{ path, store.nodes.items.len, store.edges.items.len, store.source_locations.items.len, store.errors.items.len },
    );
    for (store.nodes.items) |n| {
        try out.print("  node #{d} kind={s} name={s}\n", .{ n.id, @tagName(n.kind), n.serialized_name });
    }
}
