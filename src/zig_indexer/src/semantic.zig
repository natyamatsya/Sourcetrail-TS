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
const indexer = @import("indexer");
const InternPool = zls.analyser.InternPool;
const offsets = zls.offsets;
const Ast = std.zig.Ast;
const Storage = indexer.storage.Storage;
const NodeKind = indexer.storage.NodeKind;
const EdgeType = indexer.storage.EdgeType;
const Id = indexer.storage.Id;
const Span = indexer.storage.Span;

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

// ---------------------------------------------------------------------------
// Reference resolution pass
// ---------------------------------------------------------------------------

const DeclWithHandle = zls.Analyser.DeclWithHandle;
const DeclMap = std.AutoHashMapUnmanaged(Ast.Node.Index, indexer.parser.DeclInfo);

/// An enclosing declaration with its byte span, used to attribute a reference to
/// its context (the edge source). The innermost containing span wins.
const Context = struct { start: usize, end: usize, qual: []const u8 };

/// Resolve every identifier reference in `handle`'s file to its declaration via
/// ZLS and append resolved edges to `store`: EDGE_CALL (functions/methods),
/// EDGE_TYPE_USAGE (types), EDGE_USAGE (everything else), each with an occurrence
/// at the reference site. Targets are named the SAME way the declaration pass
/// names them (file-qualified dotted path via indexer.parser.collectDecls), so
/// nested targets (methods, fields) resolve across files too, and the reference
/// is attributed to the innermost enclosing declaration.
pub fn resolveReferences(
    session: *Session,
    store: *Storage,
    handle: *zls.DocumentStore.Handle,
    file_path: []const u8,
    file_node_id: Id,
) !void {
    const gpa = store.arena.allocator();
    const tree = &handle.tree;

    // Per-file decl maps (AST node -> name+kind), keyed by file URI, built lazily.
    var decl_maps: std.StringHashMapUnmanaged(DeclMap) = .empty;

    // Context table for THIS file: every named decl with its byte span. Built
    // up front (before any wireOne inserts into decl_maps and invalidates the
    // current-file map pointer).
    const cur_map = try getDeclMap(&decl_maps, gpa, handle.uri.raw, tree);
    var contexts: std.ArrayListUnmanaged(Context) = .empty;
    var mit = cur_map.iterator();
    while (mit.next()) |e| {
        const range = nodeByteRange(tree, e.key_ptr.*);
        try contexts.append(gpa, .{
            .start = range[0],
            .end = range[1],
            .qual = try indexer.storage.qualifiedName(gpa, file_path, e.value_ptr.name),
        });
    }

    var analyser = zls.Analyser.init(session.gpa, session.arena.allocator(), &session.store, &session.ip, handle);
    defer analyser.deinit();

    // Dedup edges by (kind, src, tgt); each reference site still adds an occurrence.
    var edges: std.StringHashMapUnmanaged(Id) = .empty;

    const tags = tree.tokens.items(.tag);
    var tok: u32 = 0;
    while (tok < tags.len) : (tok += 1) {
        if (tags[tok] != .identifier) continue;
        const idx = tree.tokens.items(.start)[tok];

        const pos = zls.Analyser.getPositionContext(session.arena.allocator(), tree, idx, true) catch continue;
        const name_tok, const name_loc = offsets.identifierTokenAndLocFromIndex(tree, idx) orelse continue;
        const name = offsets.locToSlice(tree.source, name_loc);

        var resolved: ?DeclWithHandle = null;
        var accesses: []const DeclWithHandle = &.{};
        switch (pos) {
            .var_access => resolved = (analyser.lookupSymbolGlobal(handle, name, idx) catch null),
            .field_access => |loc| {
                const held = offsets.locMerge(loc, name_loc);
                accesses = (analyser.getSymbolFieldAccesses(session.arena.allocator(), handle, idx, held, name) catch null) orelse &.{};
            },
            else => continue,
        }

        if (resolved) |d| try wireOne(store, &decl_maps, &edges, contexts.items, d, handle, tree, name_tok, file_node_id);
        for (accesses) |d| try wireOne(store, &decl_maps, &edges, contexts.items, d, handle, tree, name_tok, file_node_id);
    }
}

fn getDeclMap(cache: *std.StringHashMapUnmanaged(DeclMap), gpa: std.mem.Allocator, uri: []const u8, tree: *const Ast) !*DeclMap {
    const gop = try cache.getOrPut(gpa, uri);
    if (!gop.found_existing) gop.value_ptr.* = try indexer.parser.collectDecls(gpa, tree);
    return gop.value_ptr;
}

fn nodeByteRange(tree: *const Ast, node: Ast.Node.Index) struct { usize, usize } {
    const first = tree.firstToken(node);
    const last = tree.lastToken(node);
    return .{ tree.tokens.items(.start)[first], tree.tokens.items(.start)[last] + tree.tokenSlice(last).len };
}

fn edgeKindFor(kind: NodeKind) EdgeType {
    return switch (kind) {
        .function, .method => .call,
        .@"struct", .@"enum", .@"union", .typedef => .type_usage,
        else => .usage,
    };
}

fn wireOne(
    store: *Storage,
    decl_maps: *std.StringHashMapUnmanaged(DeclMap),
    edges: *std.StringHashMapUnmanaged(Id),
    contexts: []const Context,
    decl: DeclWithHandle,
    handle: *zls.DocumentStore.Handle,
    tree: *const Ast,
    ref_tok: Ast.TokenIndex,
    file_node_id: Id,
) !void {
    const gpa = store.arena.allocator();
    const ref_byte = tree.tokens.items(.start)[ref_tok];

    // A container-scope declaration (top-level or nested: Point, Point.add,
    // Point.x) is one the declaration pass named — wire a graph edge to it.
    // Anything else ZLS resolves an identifier to is a function-local binding
    // (var/const/parameter/capture/label): record it as a local symbol instead.
    if (decl.decl != .ast_node) return wireLocal(store, decl, handle, tree, ref_tok, file_node_id);
    const node = decl.decl.ast_node;
    const target_tree = &decl.handle.tree;
    const map = try getDeclMap(decl_maps, gpa, decl.handle.uri.raw, target_tree);
    const info = map.get(node) orelse return wireLocal(store, decl, handle, tree, ref_tok, file_node_id);

    const decl_byte = target_tree.tokens.items(.start)[decl.nameToken()];
    // Skip the declaration's own name token (a self reference).
    if (std.mem.eql(u8, decl.handle.uri.raw, handle.uri.raw) and decl_byte == ref_byte) return;

    // Name + kind exactly as the declaration pass would, so the target dedups
    // onto the right node — top-level OR nested (Point.add, Point.x).
    const target_path = try decl.handle.uri.toFsPath(gpa);
    const target_qual = try indexer.storage.qualifiedName(gpa, target_path, info.name);
    const edge_kind = edgeKindFor(info.kind);
    const target_id = try store.recordNode(info.kind, target_qual, null);

    // Context = innermost enclosing decl of the reference (smallest containing
    // span), else the file node.
    var context_id: Id = file_node_id;
    var best: usize = std.math.maxInt(usize);
    for (contexts) |ctx| {
        const w = ctx.end - ctx.start;
        if (ref_byte >= ctx.start and ref_byte < ctx.end and w < best) {
            if (store.node_by_name.get(ctx.qual)) |cid| {
                context_id = cid;
                best = w;
            }
        }
    }

    // Dedup the edge; add an occurrence for every reference site.
    const key = try std.fmt.allocPrint(gpa, "{d}:{d}:{d}", .{ @intFromEnum(edge_kind), context_id, target_id });
    const gop = try edges.getOrPut(gpa, key);
    if (!gop.found_existing) gop.value_ptr.* = try store.recordEdge(edge_kind, context_id, target_id);
    _ = try store.recordLocation(gop.value_ptr.*, file_node_id, spanOfToken(tree, ref_tok), .token);
}

/// Record a function-local binding occurrence: a LOCATION_LOCAL_SYMBOL (type 3)
/// at the reference site, tied to a local-symbol row keyed by the declaration
/// site (`<file><line:col>`, the C++ `getLocalSymbolName` convention) so the GUI
/// highlights every occurrence of the same local together. A local can only be
/// referenced within its own file, so its declaration is in `tree`.
fn wireLocal(
    store: *Storage,
    decl: DeclWithHandle,
    handle: *zls.DocumentStore.Handle,
    tree: *const Ast,
    ref_tok: Ast.TokenIndex,
    file_node_id: Id,
) !void {
    const gpa = store.arena.allocator();
    if (!std.mem.eql(u8, decl.handle.uri.raw, handle.uri.raw)) return;
    const decl_loc = tree.tokenLocation(0, decl.nameToken());
    const path = decl.handle.uri.toFsPath(gpa) catch return;
    const name = try std.fmt.allocPrint(gpa, "{s}<{d}:{d}>", .{ path, decl_loc.line + 1, decl_loc.column + 1 });
    const ls_id = try store.recordLocalSymbol(name);
    _ = try store.recordLocation(ls_id, file_node_id, spanOfToken(tree, ref_tok), .local_symbol);
}

fn spanOfToken(tree: *const Ast, tok: Ast.TokenIndex) Span {
    const loc = tree.tokenLocation(0, tok);
    const len: u32 = @intCast(tree.tokenSlice(tok).len);
    const line: u32 = @intCast(loc.line + 1);
    const col: u32 = @intCast(loc.column + 1);
    return .{ .start_line = line, .start_col = col, .end_line = line, .end_col = col + (if (len > 0) len - 1 else 0) };
}

/// Resolve every `@import("…")` in the file to its absolute on-disk path (via
/// ZLS) and emit `EDGE_INCLUDE` from this file node to the real imported file
/// node (see the note at the edge emission below for why INCLUDE, not IMPORT).
/// This is what makes the incremental reverse-dependency closure precise:
/// a raw "util.zig" node is a phantom file that never matches the real one, so
/// the syntactic pass deliberately emits no import edge. Unresolvable imports
/// (e.g. `std` with no configured zig lib dir) are skipped.
pub fn resolveImports(session: *Session, store: *Storage, handle: *zls.DocumentStore.Handle, file_node_id: Id) !void {
    const gpa = store.arena.allocator();
    const tree = &handle.tree;
    const tags = tree.tokens.items(.tag);

    var i: u32 = 0;
    while (i < tags.len) : (i += 1) {
        if (tags[i] != .builtin) continue;
        if (!std.mem.eql(u8, tree.tokenSlice(i), "@import")) continue;

        // The import string is the next string-literal token before the ')'.
        // NB: a labeled break — an unlabeled `break` inside the switch would
        // break the switch, not the loop, leaving `str` always empty.
        var j: u32 = i + 1;
        const str: []const u8 = find: while (j < tags.len and j <= i + 3) : (j += 1) {
            switch (tags[j]) {
                .string_literal => {
                    const raw = tree.tokenSlice(j);
                    break :find if (raw.len >= 2) raw[1 .. raw.len - 1] else raw;
                },
                .r_paren, .semicolon => break :find "",
                else => {},
            }
        } else "";
        if (str.len == 0) continue;

        const uri_raw = (session.resolveImport(handle, str) catch null) orelse continue;
        const path = (zls.Uri{ .raw = uri_raw }).toFsPath(gpa) catch continue;
        // Mark the imported file `indexed = true`: it is a real .zig source file
        // (own indexer command). The C++ SqliteIndexStorage::addFile reads a
        // file's content + line_count only on first insert AND only when
        // indexed, so a non-indexed import target created first would leave the
        // real file content-less — which then reads as "changed" every refresh
        // (no content to diff), breaking incremental.
        const target = try store.recordFile(path, "zig", true);
        // EDGE_INCLUDE (file -> file), not EDGE_IMPORT: Sourcetrail's
        // reverse-dependency closure (getFileIdToIncludingFileIdMap) reads
        // EDGE_INCLUDE targets as file nodes directly, whereas EDGE_IMPORT
        // expects a *symbol* target resolved to a file via an occurrence. Zig
        // `@import` is a file dependency, so INCLUDE is both correct and what
        // makes the incremental reverse-dep (edit util.zig -> reindex main.zig)
        // actually fire.
        _ = try store.recordEdge(.include, file_node_id, target);
    }
}
