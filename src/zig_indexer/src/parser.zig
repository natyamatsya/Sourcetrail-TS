//! Phase 3a: a std.zig.Ast (syntactic) symbol extractor. Walks a parsed .zig
//! file and populates a `Storage` with nodes/edges/locations. No cross-file
//! type resolution yet — that is Phase 3b (ZLS). See ROADMAP_ZIG_INDEXER.md.
//!
//! Mapping (subset of the roadmap table):
//!   const/var = struct|enum|union|opaque   -> NODE_STRUCT|ENUM|UNION|STRUCT
//!   container field / enum member           -> NODE_FIELD|ENUM_CONSTANT + EDGE_MEMBER
//!   fn                                      -> NODE_FUNCTION | NODE_METHOD (in container)
//!   other container-scope const/var         -> NODE_GLOBAL_VARIABLE
//!   @import("path")                         -> EDGE_IMPORT (file -> imported file node)

const std = @import("std");
const Ast = std.zig.Ast;
const storage = @import("storage.zig");
const Storage = storage.Storage;
const NodeKind = storage.NodeKind;
const EdgeType = storage.EdgeType;
const LocationType = storage.LocationType;
const DefinitionKind = storage.DefinitionKind;
const Id = storage.Id;
const Span = storage.Span;

pub const Error = error{OutOfMemory};

/// Parse `source` and emit its symbols into `store`. `file_path` is the logical
/// path recorded for the file node. Parse errors become StorageError rows.
pub fn indexSource(
    gpa: std.mem.Allocator,
    store: *Storage,
    file_path: []const u8,
    source: [:0]const u8,
) Error!void {
    var ast = try Ast.parse(gpa, source, .zig);
    defer ast.deinit(gpa);

    const file_id = try store.recordFile(file_path, "zig", true);

    for (ast.errors) |parse_err| {
        var aw: std.Io.Writer.Allocating = .init(gpa);
        defer aw.deinit();
        ast.renderError(parse_err, &aw.writer) catch {};
        _ = try store.recordError(aw.written(), file_path, !parse_err.is_note);
    }

    var walker: Walker = .{ .gpa = gpa, .store = store, .ast = &ast, .file_id = file_id, .file_path = file_path };
    // The file itself is a struct container; its top-level decls are members of
    // the file scope (owner = file node, but no EDGE_MEMBER at the root).
    for (ast.rootDecls()) |decl| {
        try walker.handleDecl(decl, "", file_id, false, .file);
    }
}

const Walker = struct {
    gpa: std.mem.Allocator,
    store: *Storage,
    ast: *const Ast,
    file_id: Id,
    file_path: []const u8,

    /// Build a dotted serialized name ("Outer.inner"). A simplified stand-in for
    /// Sourcetrail's NameHierarchy serialization — refined at wire integration.
    fn qualify(self: *Walker, scope: []const u8, name: []const u8) Error![]const u8 {
        const a = self.store.arena.allocator();
        if (scope.len == 0) return a.dupe(u8, name);
        return std.fmt.allocPrint(a, "{s}.{s}", .{ scope, name });
    }

    fn tokenSpan(self: *Walker, tok: Ast.TokenIndex) Span {
        const loc = self.ast.tokenLocation(0, tok);
        const slice = self.ast.tokenSlice(tok);
        const start_line: u32 = @intCast(loc.line + 1);
        const start_col: u32 = @intCast(loc.column + 1);
        const len: u32 = @intCast(slice.len);
        return .{
            .start_line = start_line,
            .start_col = start_col,
            .end_line = start_line,
            .end_col = start_col + (if (len > 0) len - 1 else 0),
        };
    }

    fn nodeScopeSpan(self: *Walker, node: Ast.Node.Index) Span {
        const first = self.ast.firstToken(node);
        const last = self.ast.lastToken(node);
        const a = self.tokenLoc(first);
        const b = self.tokenLoc(last);
        const last_slice = self.ast.tokenSlice(last);
        const last_len: u32 = @intCast(last_slice.len);
        return .{
            .start_line = @intCast(a.line + 1),
            .start_col = @intCast(a.column + 1),
            .end_line = @intCast(b.line + 1),
            .end_col = @as(u32, @intCast(b.column + 1)) + (if (last_len > 0) last_len - 1 else 0),
        };
    }

    fn tokenLoc(self: *Walker, tok: Ast.TokenIndex) Ast.Location {
        return self.ast.tokenLocation(0, tok);
    }

    /// Record a definition node: a name-token TOKEN location + a full-extent
    /// SCOPE location, mirroring ParserClientImpl.
    fn recordDef(self: *Walker, kind: NodeKind, serialized_name: []const u8, name_tok: Ast.TokenIndex, extent: Ast.Node.Index) Error!Id {
        const id = try self.store.recordNode(kind, serialized_name, .explicit);
        _ = try self.store.recordLocation(id, self.file_id, self.tokenSpan(name_tok), .token);
        _ = try self.store.recordLocation(id, self.file_id, self.nodeScopeSpan(extent), .scope);
        return id;
    }

    fn handleDecl(self: *Walker, node: Ast.Node.Index, scope: []const u8, owner_id: Id, in_container: bool, parent_kind: NodeKind) Error!void {
        const ast = self.ast;

        // Functions.
        var fn_buf: [1]Ast.Node.Index = undefined;
        if (ast.fullFnProto(&fn_buf, node)) |proto| {
            const name_tok = proto.name_token orelse return;
            const name = ast.tokenSlice(name_tok);
            const serialized = try self.qualify(scope, name);
            const kind: NodeKind = if (in_container) .method else .function;
            const id = try self.recordDef(kind, serialized, name_tok, node);
            if (in_container) _ = try self.store.recordEdge(.member, owner_id, id);
            return;
        }

        // const / var declarations.
        if (ast.fullVarDecl(node)) |var_decl| {
            const name_tok = var_decl.ast.mut_token + 1;
            const name = ast.tokenSlice(name_tok);
            const serialized = try self.qualify(scope, name);

            // const X = @import("path"): an import edge, not a symbol.
            if (var_decl.ast.init_node.unwrap()) |init_node| {
                if (self.importPath(init_node)) |path| {
                    const target = try self.importFileId(path);
                    _ = try self.store.recordEdge(.import, self.file_id, target);
                    // still record the binding as a global so it can be navigated to
                    const gid = try self.recordDef(.global_variable, serialized, name_tok, node);
                    if (in_container) _ = try self.store.recordEdge(.member, owner_id, gid);
                    return;
                }
                // const X = struct|enum|union|opaque { ... }: a container type.
                var c_buf: [2]Ast.Node.Index = undefined;
                if (ast.fullContainerDecl(&c_buf, init_node)) |container| {
                    const kind = self.containerKind(container);
                    const id = try self.recordDef(kind, serialized, name_tok, node);
                    if (in_container) _ = try self.store.recordEdge(.member, owner_id, id);
                    try self.handleContainerMembers(container, serialized, id, kind);
                    return;
                }
            }

            const id = try self.recordDef(.global_variable, serialized, name_tok, node);
            if (in_container) _ = try self.store.recordEdge(.member, owner_id, id);
            return;
        }

        // Container fields (struct field / enum member / union variant).
        if (ast.fullContainerField(node)) |field| {
            const name_tok = field.ast.main_token;
            const name = ast.tokenSlice(name_tok);
            const serialized = try self.qualify(scope, name);
            // A field of an enum is an enum constant; of a struct/union, a field.
            const kind: NodeKind = if (parent_kind == .@"enum") .enum_constant else .field;
            const id = try self.recordDef(kind, serialized, name_tok, node);
            _ = try self.store.recordEdge(.member, owner_id, id);
            return;
        }
    }

    fn handleContainerMembers(self: *Walker, container: Ast.full.ContainerDecl, scope: []const u8, owner_id: Id, kind: NodeKind) Error!void {
        for (container.ast.members) |member| {
            try self.handleDecl(member, scope, owner_id, true, kind);
        }
    }

    fn containerKind(self: *Walker, container: Ast.full.ContainerDecl) NodeKind {
        const kw = self.ast.tokenTag(container.ast.main_token);
        return switch (kw) {
            .keyword_struct => .@"struct",
            .keyword_enum => .@"enum",
            .keyword_union => .@"union",
            .keyword_opaque => .@"struct",
            else => .@"struct",
        };
    }

    /// If `node` is `@import("literal")`, return the unquoted literal.
    fn importPath(self: *Walker, node: Ast.Node.Index) ?[]const u8 {
        const ast = self.ast;
        const main = ast.nodeMainToken(node);
        if (ast.tokenTag(main) != .builtin) return null;
        if (!std.mem.eql(u8, ast.tokenSlice(main), "@import")) return null;
        // The first (and only) argument is the string-literal node: its main
        // token is the string literal. Scan forward to the next string token.
        var t = main + 1;
        while (t < ast.tokens.len) : (t += 1) {
            switch (ast.tokenTag(t)) {
                .string_literal => {
                    const raw = ast.tokenSlice(t);
                    if (raw.len >= 2) return raw[1 .. raw.len - 1];
                    return raw;
                },
                .r_paren, .semicolon => return null,
                else => {},
            }
        }
        return null;
    }

    fn importFileId(self: *Walker, path: []const u8) Error!Id {
        // Phase 3a records the import target as a file node keyed by the raw
        // import string. Phase 3b resolves it to the real on-disk path.
        if (self.store.node_by_name.get(path)) |id| return id;
        return self.store.recordFile(path, "zig", false);
    }
};
