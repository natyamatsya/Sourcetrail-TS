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

// ---------------------------------------------------------------------------
// Shared declaration classification
//
// Both passes below map an AST decl node to the same (name, NodeKind): the
// Walker *emits* nodes/edges/locations, the Collector builds a node->name map
// the ZLS reference pass names targets from. Classifying in ONE place keeps the
// two passes from drifting (a resolved target must dedup onto the exact node the
// declaration pass created).
// ---------------------------------------------------------------------------

/// What a container-scope declaration maps to: the token holding its name, the
/// Sourcetrail node kind, and — for a `const X = struct|enum|union|opaque {…}`
/// type — the container whose members should be recursed into.
const DeclClass = struct {
    name_token: Ast.TokenIndex,
    kind: NodeKind,
    container: ?Ast.full.ContainerDecl = null,
};

/// Join a dotted scope path: `"" + "x" -> "x"`, `"Outer" + "x" -> "Outer.x"`.
fn qualify(a: std.mem.Allocator, scope: []const u8, name: []const u8) Error![]const u8 {
    if (scope.len == 0) return a.dupe(u8, name);
    return std.fmt.allocPrint(a, "{s}.{s}", .{ scope, name });
}

/// struct / enum / union / opaque keyword -> the matching container NodeKind
/// (opaque has no distinct kind, so it maps to struct).
fn containerKindOf(tree: *const Ast, container: Ast.full.ContainerDecl) NodeKind {
    return switch (tree.tokenTag(container.ast.main_token)) {
        .keyword_enum => .@"enum",
        .keyword_union => .@"union",
        else => .@"struct",
    };
}

/// Classify a declaration node. `buf` must outlive the result (it backs the
/// returned `ContainerDecl`). `in_container` selects method-vs-function;
/// `parent_kind` selects enum-constant-vs-field. Returns null for nodes that are
/// not named declarations. Note `const X = @import("…")` is not a container, so
/// it falls through to `.global_variable` — the resolved EDGE_INCLUDE to the
/// imported file is emitted later by the ZLS pass (semantic.zig), which needs to
/// resolve the import string to a real path.
fn classifyDecl(
    tree: *const Ast,
    buf: *[2]Ast.Node.Index,
    node: Ast.Node.Index,
    in_container: bool,
    parent_kind: NodeKind,
) ?DeclClass {
    if (tree.fullFnProto(buf[0..1], node)) |proto| {
        const name_token = proto.name_token orelse return null;
        return .{ .name_token = name_token, .kind = if (in_container) .method else .function };
    }
    if (tree.fullVarDecl(node)) |var_decl| {
        const name_token = var_decl.ast.mut_token + 1;
        if (var_decl.ast.init_node.unwrap()) |init_node| {
            if (tree.fullContainerDecl(buf, init_node)) |container| {
                return .{ .name_token = name_token, .kind = containerKindOf(tree, container), .container = container };
            }
        }
        return .{ .name_token = name_token, .kind = .global_variable };
    }
    if (tree.fullContainerField(node)) |field| {
        return .{ .name_token = field.ast.main_token, .kind = if (parent_kind == .@"enum") .enum_constant else .field };
    }
    return null;
}

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

/// Name + kind of a declaration, keyed by its AST node — the inverse of the
/// emission pass, used by the ZLS reference pass to name a resolved target the
/// SAME way the declaration pass named it (so cross-file targets dedup). Mirrors
/// `Walker.handleDecl` exactly; `name` is the plain dotted local path (no file
/// qualifier).
pub const DeclInfo = struct { name: []const u8, kind: NodeKind };

/// Map every named declaration in `tree` to its dotted local name + kind.
/// Allocated with `gpa` (names too); caller owns the map.
pub fn collectDecls(gpa: std.mem.Allocator, tree: *const Ast) Error!std.AutoHashMapUnmanaged(Ast.Node.Index, DeclInfo) {
    var map: std.AutoHashMapUnmanaged(Ast.Node.Index, DeclInfo) = .empty;
    errdefer map.deinit(gpa);
    var c = Collector{ .gpa = gpa, .tree = tree, .map = &map };
    for (tree.rootDecls()) |node| try c.visit(node, "", false, .file);
    return map;
}

const Collector = struct {
    gpa: std.mem.Allocator,
    tree: *const Ast,
    map: *std.AutoHashMapUnmanaged(Ast.Node.Index, DeclInfo),

    fn visit(self: *Collector, node: Ast.Node.Index, scope: []const u8, in_container: bool, parent_kind: NodeKind) Error!void {
        var buf: [2]Ast.Node.Index = undefined;
        const cls = classifyDecl(self.tree, &buf, node, in_container, parent_kind) orelse return;
        const local = try qualify(self.gpa, scope, self.tree.tokenSlice(cls.name_token));
        try self.map.put(self.gpa, node, .{ .name = local, .kind = cls.kind });
        if (cls.container) |container| {
            for (container.ast.members) |member| try self.visit(member, local, true, cls.kind);
        }
    }
};

const Walker = struct {
    gpa: std.mem.Allocator,
    store: *Storage,
    ast: *const Ast,
    file_id: Id,
    file_path: []const u8,

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
    /// SCOPE location, mirroring ParserClientImpl. `local_name` is the plain
    /// dotted path within the file; it is file-qualified here (see
    /// storage.qualifiedName) so the name is globally unique.
    fn recordDef(self: *Walker, kind: NodeKind, local_name: []const u8, name_tok: Ast.TokenIndex, extent: Ast.Node.Index) Error!Id {
        const full = try storage.qualifiedName(self.store.arena.allocator(), self.file_path, local_name);
        const id = try self.store.recordNode(kind, full, .explicit);
        _ = try self.store.recordLocation(id, self.file_id, self.tokenSpan(name_tok), .token);
        _ = try self.store.recordLocation(id, self.file_id, self.nodeScopeSpan(extent), .scope);
        return id;
    }

    fn handleDecl(self: *Walker, node: Ast.Node.Index, scope: []const u8, owner_id: Id, in_container: bool, parent_kind: NodeKind) Error!void {
        var buf: [2]Ast.Node.Index = undefined;
        const cls = classifyDecl(self.ast, &buf, node, in_container, parent_kind) orelse return;

        const serialized = try qualify(self.store.arena.allocator(), scope, self.ast.tokenSlice(cls.name_token));
        const id = try self.recordDef(cls.kind, serialized, cls.name_token, node);
        // A container member gets an EDGE_MEMBER to its owner. A field/enum
        // constant is always a member (a Zig file is itself a struct, so a
        // top-level field is a member of the file node); a nested fn/var is a
        // member of its container, but a *top-level* fn/var is not (its file
        // membership is implicit — no edge). A `const X = @import("path")`
        // lands here as .global_variable; the resolved EDGE_INCLUDE to the
        // imported file is emitted by the ZLS pass (semantic.zig).
        const is_member = switch (cls.kind) {
            .field, .enum_constant => true,
            else => in_container,
        };
        if (is_member) _ = try self.store.recordEdge(.member, owner_id, id);
        if (cls.container) |container| {
            for (container.ast.members) |member| try self.handleDecl(member, serialized, id, true, cls.kind);
        }
    }
};
