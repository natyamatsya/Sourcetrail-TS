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

/// A top-level declaration in the file being indexed, with its byte span — used
/// to attribute a reference to the enclosing declaration (the edge's context).
const TopDecl = struct { start: usize, end: usize, qual: []const u8 };

/// Resolve every identifier reference in `handle`'s file to its declaration via
/// ZLS and append resolved edges to `store`: EDGE_CALL to functions, EDGE_USAGE
/// otherwise, each with an occurrence at the reference site. Only references
/// whose target is a *top-level* declaration are wired (their file-qualified
/// name is unambiguous and matches the declaration pass; nested targets need a
/// scope-path name and are left for a follow-up). `file_path` is the absolute
/// path of the indexed file; `file_node_id` its NODE_FILE id.
pub fn resolveReferences(
    session: *Session,
    store: *Storage,
    handle: *zls.DocumentStore.Handle,
    file_path: []const u8,
    file_node_id: Id,
) !void {
    const gpa = store.arena.allocator();
    const tree = &handle.tree;

    // Enclosing-context table: this file's top-level decls with their byte spans.
    var contexts: std.ArrayListUnmanaged(TopDecl) = .empty;
    for (tree.rootDecls()) |node| {
        const name_tok = declNameToken(tree, node) orelse continue;
        const first = tree.firstToken(node);
        const last = tree.lastToken(node);
        const start = tree.tokens.items(.start)[first];
        const end = tree.tokens.items(.start)[last] + tree.tokenSlice(last).len;
        const local = tree.tokenSlice(name_tok);
        try contexts.append(gpa, .{
            .start = start,
            .end = end,
            .qual = try indexer.storage.qualifiedName(gpa, file_path, local),
        });
    }

    var analyser = zls.Analyser.init(session.gpa, session.arena.allocator(), &session.store, &session.ip, handle);
    defer analyser.deinit();

    // Dedup edges by (kind, src, tgt); each reference site still adds an occurrence.
    var edges = std.StringHashMapUnmanaged(Id).empty;

    const tags = tree.tokens.items(.tag);
    var tok: u32 = 0;
    while (tok < tags.len) : (tok += 1) {
        if (tags[tok] != .identifier) continue;
        const idx = tree.tokens.items(.start)[tok];

        const pos = zls.Analyser.getPositionContext(session.arena.allocator(), tree, idx, true) catch continue;
        const name_tok, const name_loc = offsets.identifierTokenAndLocFromIndex(tree, idx) orelse continue;
        const name = offsets.locToSlice(tree.source, name_loc);

        var resolved: ?zls.Analyser.DeclWithHandle = null;
        var accesses: []const zls.Analyser.DeclWithHandle = &.{};
        switch (pos) {
            .var_access => resolved = (analyser.lookupSymbolGlobal(handle, name, idx) catch null),
            .field_access => |loc| {
                const held = offsets.locMerge(loc, name_loc);
                accesses = (analyser.getSymbolFieldAccesses(session.arena.allocator(), handle, idx, held, name) catch null) orelse &.{};
            },
            else => continue,
        }

        if (resolved) |d| try wireOne(store, &edges, d, handle, tree, name_tok, contexts.items, file_node_id);
        for (accesses) |d| try wireOne(store, &edges, d, handle, tree, name_tok, contexts.items, file_node_id);
    }
}

fn wireOne(
    store: *Storage,
    edges: *std.StringHashMapUnmanaged(Id),
    decl: zls.Analyser.DeclWithHandle,
    handle: *zls.DocumentStore.Handle,
    tree: *const Ast,
    ref_tok: Ast.TokenIndex,
    contexts: []const TopDecl,
    file_node_id: Id,
) !void {
    const gpa = store.arena.allocator();

    // Only AST-node declarations that are top-level in their own file.
    if (decl.decl != .ast_node) return;
    const node = decl.decl.ast_node;
    const target_tree = &decl.handle.tree;
    if (!isRootDecl(target_tree, node)) return;

    const decl_name_tok = decl.nameToken();
    const decl_byte = decl.handle.tree.tokens.items(.start)[decl_name_tok];
    const ref_byte = tree.tokens.items(.start)[ref_tok];
    // Skip the declaration's own name token (a self reference).
    if (std.mem.eql(u8, decl.handle.uri.raw, handle.uri.raw) and decl_byte == ref_byte) return;

    const target_path = try decl.handle.uri.toFsPath(gpa);
    const target_local = target_tree.tokenSlice(decl_name_tok);
    const target_qual = try indexer.storage.qualifiedName(gpa, target_path, target_local);

    // Function target -> CALL; anything else -> USAGE.
    const is_fn = target_tree.nodeTag(node) == .fn_decl;
    const node_kind: NodeKind = if (is_fn) .function else .symbol;
    const edge_kind: EdgeType = if (is_fn) .call else .usage;

    const target_id = try store.recordNode(node_kind, target_qual, null);

    // Context = enclosing top-level decl of the reference (else the file node).
    var context_id: Id = file_node_id;
    for (contexts) |ctx| {
        if (ref_byte >= ctx.start and ref_byte < ctx.end) {
            if (store.node_by_name.get(ctx.qual)) |cid| context_id = cid;
            break;
        }
    }

    // Dedup the edge; add an occurrence for every reference site.
    const key = try std.fmt.allocPrint(gpa, "{d}:{d}:{d}", .{ @intFromEnum(edge_kind), context_id, target_id });
    const gop = try edges.getOrPut(gpa, key);
    if (!gop.found_existing) gop.value_ptr.* = try store.recordEdge(edge_kind, context_id, target_id);
    _ = try store.recordLocation(gop.value_ptr.*, file_node_id, spanOfToken(tree, ref_tok), .token);
}

fn declNameToken(tree: *const Ast, node: Ast.Node.Index) ?Ast.TokenIndex {
    var buf: [1]Ast.Node.Index = undefined;
    if (tree.fullFnProto(&buf, node)) |proto| return proto.name_token;
    if (tree.fullVarDecl(node)) |vd| return vd.ast.mut_token + 1;
    return null;
}

fn isRootDecl(tree: *const Ast, node: Ast.Node.Index) bool {
    for (tree.rootDecls()) |r| if (r == node) return true;
    return false;
}

fn spanOfToken(tree: *const Ast, tok: Ast.TokenIndex) Span {
    const loc = tree.tokenLocation(0, tok);
    const len: u32 = @intCast(tree.tokenSlice(tok).len);
    const line: u32 = @intCast(loc.line + 1);
    const col: u32 = @intCast(loc.column + 1);
    return .{ .start_line = line, .start_col = col, .end_line = line, .end_col = col + (if (len > 0) len - 1 else 0) };
}
