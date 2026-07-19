//! Phase 3b: cross-file semantic resolution via ZLS (0.16.0).
//!
//! `Session` wraps ZLS's `DocumentStore` (loads files + resolves `@import`),
//! `Analyser` (resolves identifiers/calls/types to their declarations), and an
//! `InternPool`. This recovers the cross-file information the syntactic Phase-3a
//! parser cannot: a reference's *definition* — which file it lives in and what
//! it is named — the basis for resolved `EDGE_CALL` / `EDGE_TYPE_USAGE` targets.
//!
//! Setup mirrors ZLS's own `tests/analysis_check.zig` harness.

const std = @import("std");
const zls = @import("zls");
const InternPool = zls.analyser.InternPool;

pub const Resolved = struct {
    /// Name of the resolved declaration (slice into its file's source).
    name: []const u8,
    /// URI of the file the declaration lives in.
    uri: []const u8,
};

pub const Session = struct {
    gpa: std.mem.Allocator,
    io: std.Io,
    arena: std.heap.ArenaAllocator,
    ip: InternPool,
    diagnostics: zls.DiagnosticsCollection,
    environ_map: std.process.Environ.Map,
    store: zls.DocumentStore,

    pub fn init(gpa: std.mem.Allocator, io: std.Io, zig_exe_path: ?[]const u8) !Session {
        var self: Session = undefined;
        self.gpa = gpa;
        self.io = io;
        self.arena = std.heap.ArenaAllocator.init(gpa);
        self.ip = try InternPool.init(io, gpa);
        self.diagnostics = .{ .io = io, .allocator = gpa };
        self.environ_map = .init(gpa);
        self.store = .{
            .io = io,
            .allocator = gpa,
            .config = .{
                .environ_map = &self.environ_map,
                .zig_exe_path = zig_exe_path,
                .zig_lib_dir = null,
                .build_runner_path = null,
                .builtin_path = null,
                .global_cache_dir = null,
                .wasi_preopens = {},
            },
            .diagnostics_collection = &self.diagnostics,
        };
        return self;
    }

    pub fn deinit(self: *Session) void {
        self.store.deinit();
        self.diagnostics.deinit();
        self.environ_map.deinit();
        self.ip.deinit(self.gpa);
        self.arena.deinit();
    }

    /// Register a document by absolute path with the given source text (kept in
    /// memory; no disk read). Returns its handle.
    pub fn openDocument(self: *Session, path: []const u8, source: [:0]const u8) !*zls.DocumentStore.Handle {
        const uri = try zls.Uri.fromPath(self.arena.allocator(), path);
        try self.store.openLspSyncedDocument(uri, source);
        return self.store.getHandle(uri).?;
    }

    /// Resolve the identifier `name` visible at byte offset `source_index` in
    /// `handle` to its declaration (across imports). Null if unresolved.
    pub fn lookupGlobal(self: *Session, handle: *zls.DocumentStore.Handle, name: []const u8, source_index: usize) !?Resolved {
        var analyser = zls.Analyser.init(self.gpa, self.arena.allocator(), &self.store, &self.ip, handle);
        defer analyser.deinit();

        const decl = (try analyser.lookupSymbolGlobal(handle, name, source_index)) orelse return null;
        const tok = decl.nameToken();
        return .{
            .name = decl.handle.tree.tokenSlice(tok),
            .uri = decl.handle.uri.raw,
        };
    }

    /// Resolve an `@import("…")` string to the URI of the target file.
    pub fn resolveImport(self: *Session, handle: *zls.DocumentStore.Handle, import_str: []const u8) !?[]const u8 {
        const result = try self.store.uriFromImportStr(self.arena.allocator(), handle, import_str);
        return switch (result) {
            .one => |uri| uri.raw,
            .none => null,
            else => null,
        };
    }
};
