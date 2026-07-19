//! `sourcetrail_zig_indexer` entry point.
//!
//! Two modes:
//!  - **IPC mode** (how the app launches it): positional argv
//!    `<processId> <instanceUuid> <appPath> <userDataPath> <logFilePath>`.
//!    Opens the thoth-ipc channels and runs
//!    `check interrupt -> pop Zig command -> index -> push storage -> finish`.
//!  - **standalone mode**: given .zig file paths, parse each and print a summary
//!    (a smoke driver for the parser). Selected when argv[1] is not a number.

const std = @import("std");
const indexer = @import("indexer");
const semantic = @import("semantic.zig");
const CommandChannel = @import("ipc/command.zig").CommandChannel;
const StatusChannel = @import("ipc/status.zig").StatusChannel;
const StorageChannel = @import("ipc/storage_channel.zig").StorageChannel;

pub fn main(init: std.process.Init) !void {
    const gpa = init.gpa;

    var args = try init.minimal.args.iterateAllocator(gpa);
    defer args.deinit();
    const prog = args.next() orelse "sourcetrail_zig_indexer";

    // Collect argv into a slice we can index.
    var argv: std.ArrayListUnmanaged([]const u8) = .empty;
    defer {
        for (argv.items) |a| gpa.free(a);
        argv.deinit(gpa);
    }
    while (args.next()) |a| try argv.append(gpa, try gpa.dupe(u8, a));

    // Semantic smoke: `--resolve <file.zig> <identifier>` prints the ZLS-resolved
    // declaration (name + file), proving cross-file resolution end to end.
    if (argv.items.len >= 3 and std.mem.eql(u8, argv.items[0], "--resolve")) {
        try runResolve(gpa, init.io, argv.items[1], argv.items[2]);
        return;
    }

    // IPC mode when argv[0] is a numeric processId.
    if (argv.items.len >= 2) {
        if (std.fmt.parseInt(u64, argv.items[0], 10)) |process_id| {
            try runIpc(gpa, init.io, process_id, argv.items[1]);
            return;
        } else |_| {}
    }

    try runStandalone(gpa, init.io, prog, argv.items);
}

fn runIpc(gpa: std.mem.Allocator, io: std.Io, process_id: u64, uuid: []const u8) !void {
    var commands = try CommandChannel.open(uuid);
    defer commands.deinit();
    var status = try StatusChannel.open(uuid, process_id);
    defer status.deinit();
    var storage = try StorageChannel.open(uuid, process_id);
    defer storage.deinit();

    while (true) {
        if (try status.isInterrupted()) break;

        const maybe_cmd = try commands.popZig(gpa);
        var cmd = maybe_cmd orelse break;
        defer cmd.deinit(gpa);

        try status.updateIndexing(gpa, cmd.source_file_path);
        indexOneCommand(gpa, io, &storage, cmd.source_file_path) catch {};
        try status.finishIndexing(gpa);
    }
}

fn indexOneCommand(gpa: std.mem.Allocator, io: std.Io, storage: *StorageChannel, path: []const u8) !void {
    const source = std.Io.Dir.cwd().readFileAllocOptions(
        io,
        path,
        gpa,
        .limited(64 * 1024 * 1024),
        .of(u8),
        0,
    ) catch return;
    defer gpa.free(source);

    var store = indexer.Storage.init(gpa);
    defer store.deinit();
    try indexer.indexSource(gpa, &store, path, source);

    // Back-pressure: wait for the app to drain before pushing more.
    const nap = std.c.timespec{ .sec = 0, .nsec = 2 * std.time.ns_per_ms };
    while ((try storage.count()) >= 2) _ = std.c.nanosleep(&nap, null);
    try storage.push(gpa, &store);
}

fn runResolve(gpa: std.mem.Allocator, io: std.Io, path: []const u8, name: []const u8) !void {
    const source = try std.Io.Dir.cwd().readFileAllocOptions(io, path, gpa, .limited(16 * 1024 * 1024), .of(u8), 0);
    defer gpa.free(source);

    var session = try semantic.Session.init(gpa, io, null);
    defer session.deinit();

    const handle = try session.openDocument(path, source);
    // Resolve at end-of-file (global scope) so top-level decls are in view.
    const resolved = try session.lookupGlobal(handle, name, source.len - 1);

    var buf: [4096]u8 = undefined;
    var w = std.Io.File.stdout().writer(io, &buf);
    const out = &w.interface;
    if (resolved) |r| {
        try out.print("resolved '{s}' -> decl '{s}' in {s}\n", .{ name, r.name, r.uri });
    } else {
        try out.print("resolved '{s}' -> (unresolved)\n", .{name});
    }
    try out.flush();
}

fn runStandalone(gpa: std.mem.Allocator, io: std.Io, prog: []const u8, argv: []const []const u8) !void {
    var stdout_buf: [4096]u8 = undefined;
    var stdout_writer = std.Io.File.stdout().writer(io, &stdout_buf);
    const out = &stdout_writer.interface;

    if (argv.len == 0) {
        try out.print("usage: {s} <file.zig> [more.zig ...]\n", .{prog});
        try out.flush();
        return;
    }
    for (argv) |path| try indexOne(io, gpa, out, path);
    try out.flush();
}

fn indexOne(io: std.Io, gpa: std.mem.Allocator, out: *std.Io.Writer, path: []const u8) !void {
    const source = std.Io.Dir.cwd().readFileAllocOptions(io, path, gpa, .limited(16 * 1024 * 1024), .of(u8), 0) catch |err| {
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
