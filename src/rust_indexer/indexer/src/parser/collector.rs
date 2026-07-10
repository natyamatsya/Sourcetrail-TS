use std::collections::{HashMap, HashSet};
use std::sync::atomic::{AtomicUsize, Ordering};

use either::Either;
use ra_ap_hir::{
    Adt, AsAssocItem, AssocItem, Crate, FieldSource, HasSource, ModuleDef, PathResolution,
    Semantics,
};
use ra_ap_ide_db::RootDatabase;
use ra_ap_ide_db::base_db::SourceDatabase;
use ra_ap_ide_db::line_index::{LineCol, LineIndex};
use ra_ap_syntax::ast::{self, HasName};
use ra_ap_syntax::{AstNode, SyntaxKind};
use ra_ap_vfs::Vfs;

use crate::ipc::storage::{
    OwnedIntermediateStorage, OwnedStorageEdge, OwnedStorageError, OwnedStorageFile,
    OwnedStorageNode, OwnedStorageOccurrence, OwnedStorageSourceLocation, OwnedStorageSymbol,
};

use super::{
    DEFINITION_EXPLICIT, DEFINITION_IMPLICIT, EDGE_CALL, EDGE_INHERITANCE, EDGE_MEMBER,
    EDGE_OVERRIDE, EDGE_TYPE_ARGUMENT, EDGE_TYPE_USAGE, EDGE_USAGE, LOCATION_SCOPE, LOCATION_TOKEN,
    NODE_ENUM, NODE_ENUM_CONSTANT, NODE_FIELD, NODE_FILE, NODE_FUNCTION, NODE_GLOBAL_VARIABLE,
    NODE_INTERFACE, NODE_MACRO, NODE_METHOD, NODE_MODULE, NODE_STRUCT, NODE_TYPE_PARAMETER,
    NODE_TYPEDEF, NODE_UNION,
};

/// Identity key for an emitted definition, so that semantically resolved
/// references (Semantics::resolve_path / resolve_method_call) can be mapped
/// back to the exact node — no string matching involved.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
enum DefKey {
    Def(ModuleDef),
    Field(ra_ap_hir::Field),
    TypeParam(ra_ap_hir::TypeParam),
    ConstParam(ra_ap_hir::ConstParam),
    LifetimeParam(ra_ap_hir::LifetimeParam),
}

/// Where a type-bound reference originates (see
/// context/DESIGN_RUST_TYPE_SYSTEM_EDGES.md §2/§4).
enum BoundOwner {
    /// A generic parameter carries the bound (`T: Trait`, `'a: 'b`).
    Param(i64),
    /// A supertrait bound (`trait A: B`) — becomes EDGE_INHERITANCE.
    Trait(i64),
    /// Not a generic-param bound (plain type position, `dyn`/`impl` trait).
    Item,
}

/// Cached per-file location context: the emitted file node plus the line
/// index for offset → line/col conversion. Built once per file instead of
/// per emitted item.
struct FileInfo {
    file_node_id: i64,
    line_index: LineIndex,
}

/// Text range of a path's last segment name token — the clickable reference token.
fn path_name_range(path: &ast::Path) -> Option<ra_ap_syntax::TextRange> {
    Some(path.segment()?.name_ref()?.syntax().text_range())
}

/// One resolved reference from the (parallelizable) resolution step: an edge
/// plus an optional TOKEN occurrence range in the file being resolved.
/// Applying a row = `Collector::push_edge` (dedup) + occurrence location.
struct ReferenceRow {
    edge_type: i32,
    source_id: i64,
    target_id: i64,
    /// Reference-site token range, when the reference has a clickable token
    /// (impl overrides do not).
    range: Option<ra_ap_syntax::TextRange>,
}

/// The pure resolution half of the semantic reference pass: turns one file's
/// syntax into `ReferenceRow`s using only shared read access to the database
/// and the definition maps — safe to fan out across worker threads.
struct RefResolver<'a> {
    db: &'a RootDatabase,
    /// HIR definition identity → node id (exact resolution)
    def_ids: &'a HashMap<DefKey, i64>,
    /// plain qualified name → node id (fallback resolution)
    node_ids: &'a HashMap<String, i64>,
}

struct Collector<'db> {
    db: &'db RootDatabase,
    vfs: &'db Vfs,
    /// file_path → StorageFile id
    file_ids: HashMap<String, i64>,
    /// vfs file → cached location context (`None` = the vfs id does not
    /// resolve to a path; the failure is cached too)
    file_infos: HashMap<ra_ap_vfs::FileId, Option<FileInfo>>,
    /// plain qualified name → node id (fallback edge resolution)
    node_ids: HashMap<String, i64>,
    /// HIR definition identity → node id (exact edge resolution)
    def_ids: HashMap<DefKey, i64>,
    /// local source files, in discovery order, for the semantic reference pass
    local_files: Vec<ra_ap_vfs::FileId>,
    local_files_seen: HashSet<ra_ap_vfs::FileId>,
    /// (edge type, source, target) → edge id, for dedup and occurrence linking
    emitted_edges: HashMap<(i32, i64, i64), i64>,
    next_id: i64,
    storage: OwnedIntermediateStorage,
    /// Called whenever we start processing a new source file. Receives the file path.
    on_file: Box<dyn FnMut(&str) + 'db>,
}

/// Encode a `::` -delimited qualified name into the `NameHierarchy` wire format:
///   `"::\tm" + parts.join("\tn" + each_part + "\ts\tp") + "\ts\tp"`
/// where `\t` is a literal tab.
fn serialize_name(qualified: &str) -> String {
    let mut out = String::from("::\tm");
    let parts: Vec<&str> = qualified.split("::").collect();
    for (i, part) in parts.iter().enumerate() {
        if i > 0 {
            out.push('\t');
            out.push('n');
        }
        out.push_str(part);
        out.push('\t');
        out.push('s');
        out.push('\t');
        out.push('p');
    }
    out
}

/// Encode a file path into the `NameHierarchy` wire format using "/" delimiter:
///   `"/\tm" + path + "\ts\tp"`
fn serialize_file_name(path: &str) -> String {
    let mut out = String::from("/\tm");
    out.push_str(path);
    out.push('\t');
    out.push('s');
    out.push('\t');
    out.push('p');
    out
}

impl<'db> Collector<'db> {
    fn with_callback(
        db: &'db RootDatabase,
        vfs: &'db Vfs,
        on_file: impl FnMut(&str) + 'db,
    ) -> Self {
        Self {
            db,
            vfs,
            file_ids: HashMap::new(),
            file_infos: HashMap::new(),
            node_ids: HashMap::new(),
            def_ids: HashMap::new(),
            local_files: Vec::new(),
            local_files_seen: HashSet::new(),
            emitted_edges: HashMap::new(),
            next_id: 1,
            storage: OwnedIntermediateStorage::default(),
            on_file: Box::new(on_file),
        }
    }

    /// Remember an emitted definition under its HIR identity so references can
    /// be resolved exactly. Call right after the node was emitted under `name`.
    fn register_def(&mut self, key: DefKey, name: &str) {
        if let Some(&id) = self.node_ids.get(name) {
            self.def_ids.insert(key, id);
        }
    }

    /// Read-only resolver over the definition maps — used by the semantic
    /// reference pass and by pass-1 name-fallback edge resolution.
    fn resolver(&self) -> RefResolver<'_> {
        RefResolver {
            db: self.db,
            def_ids: &self.def_ids,
            node_ids: &self.node_ids,
        }
    }

    fn record_local_file(&mut self, file_id: ra_ap_vfs::FileId) {
        if self.local_files_seen.insert(file_id) {
            self.local_files.push(file_id);
        }
    }

    fn alloc_id(&mut self) -> i64 {
        let id = self.next_id;
        self.next_id += 1;
        id
    }

    fn file_node_id(&mut self, path: &str) -> i64 {
        if let Some(&id) = self.file_ids.get(path) {
            return id;
        }
        let id = self.alloc_id();
        self.file_ids.insert(path.to_owned(), id);
        self.storage.nodes.push(OwnedStorageNode {
            id,
            type_: NODE_FILE,
            serialized_name: serialize_file_name(path),
        });
        self.storage.files.push(OwnedStorageFile {
            id,
            file_path: path.to_owned(),
            language_identifier: "rust".to_owned(),
            indexed: true,
            complete: true,
        });
        id
    }

    /// Cached per-file location context for `file_id`, populated on first
    /// use (resolving the path, allocating the file node, and building the
    /// `LineIndex`). `None` if the file does not resolve to a path.
    fn file_info(&mut self, file_id: ra_ap_vfs::FileId) -> Option<&FileInfo> {
        if !self.file_infos.contains_key(&file_id) {
            // Resolve path and file node id before inserting: `file_node_id`
            // mutates storage and must not overlap a cache borrow.
            let info = self.vfs_path(file_id).map(|path| FileInfo {
                file_node_id: self.file_node_id(&path),
                line_index: LineIndex::new(self.db.file_text(file_id).text(self.db)),
            });
            self.file_infos.insert(file_id, info);
        }
        self.file_infos.get(&file_id)?.as_ref()
    }

    /// Convert `range` in `file_id` to line/col coordinates via the per-file
    /// cache. Returns `None` if the file has no resolvable path or the range
    /// does not fit the file text (e.g. an unmapped macro expansion) — the
    /// non-panicking `try_line_col` behavior is kept.
    fn location_of(
        &mut self,
        file_id: ra_ap_vfs::FileId,
        range: ra_ap_syntax::TextRange,
    ) -> Option<(i64, LineCol, LineCol)> {
        let info = self.file_info(file_id)?;
        let start = info.line_index.try_line_col(range.start())?;
        let end = info.line_index.try_line_col(range.end())?;
        Some((info.file_node_id, start, end))
    }

    fn add_def(
        &mut self,
        name: &str,
        node_kind: i32,
        vfs_file_id: ra_ap_vfs::FileId,
        range: ra_ap_syntax::TextRange,
    ) {
        self.add_def_kind(name, node_kind, vfs_file_id, range, DEFINITION_EXPLICIT);
    }

    fn add_def_kind(
        &mut self,
        name: &str,
        node_kind: i32,
        vfs_file_id: ra_ap_vfs::FileId,
        range: ra_ap_syntax::TextRange,
        definition_kind: i32,
    ) {
        let node_id = self.alloc_id();
        let loc_id = self.alloc_id();

        self.node_ids.insert(name.to_owned(), node_id);
        self.storage.nodes.push(OwnedStorageNode {
            id: node_id,
            type_: node_kind,
            serialized_name: serialize_name(name),
        });
        self.storage.symbols.push(OwnedStorageSymbol {
            id: node_id,
            definition_kind,
        });

        // A range that does not fit the file text (e.g. an unmapped macro
        // expansion) must not kill the whole indexing run — emit the node
        // without a location instead.
        let Some((file_node_id, start, end)) = self.location_of(vfs_file_id, range) else {
            return;
        };

        self.storage
            .source_locations
            .push(OwnedStorageSourceLocation {
                id: loc_id,
                file_node_id,
                start_line: start.line + 1,
                start_col: start.col + 1,
                end_line: end.line + 1,
                end_col: end.col + 1,
                type_: LOCATION_TOKEN,
            });
        self.storage.occurrences.push(OwnedStorageOccurrence {
            element_id: node_id,
            source_location_id: loc_id,
        });
    }

    /// Map a (possibly macro-expansion) range to real-file coordinates.
    /// For real files this is the identity; for macro files the range maps up
    /// through the expansion, falling back to the macro call site (e.g. the
    /// `#[derive(…)]` attribute) when the token has no real-source equivalent.
    fn original_location(
        &self,
        file_id: ra_ap_hir::HirFileId,
        range: ra_ap_syntax::TextRange,
    ) -> Option<(ra_ap_vfs::FileId, ra_ap_syntax::TextRange)> {
        let mapped =
            ra_ap_hir::InFile::new(file_id, range).original_node_file_range_rooted(self.db);
        let vfs_fid = mapped.file_id.file_id(self.db);
        // Items in files without a resolvable path are skipped entirely,
        // exactly like before the cache existed.
        self.vfs_path(vfs_fid)?;
        Some((vfs_fid, mapped.range))
    }

    /// Resolve a `ra_ap_vfs::FileId` to an absolute path string, if possible.
    fn vfs_path(&self, file_id: ra_ap_vfs::FileId) -> Option<String> {
        self.vfs.file_path(file_id).as_path().map(|p| p.to_string())
    }

    /// Emit a directed edge between two node ids, deduplicating identical
    /// (type, source, target) triples. Returns the edge id (existing on dedup),
    /// so reference occurrences can attach to it.
    fn push_edge(&mut self, edge_type: i32, source_id: i64, target_id: i64) -> i64 {
        if let Some(&id) = self.emitted_edges.get(&(edge_type, source_id, target_id)) {
            return id;
        }
        let edge_id = self.alloc_id();
        self.emitted_edges
            .insert((edge_type, source_id, target_id), edge_id);
        self.storage.edges.push(OwnedStorageEdge {
            id: edge_id,
            type_: edge_type,
            source_node_id: source_id,
            target_node_id: target_id,
        });
        edge_id
    }

    /// Emit a directed edge between two named nodes (looked up by serialized name).
    /// If either node hasn't been emitted yet the edge is silently dropped — this
    /// can happen for items from external crates that we intentionally skip.
    fn add_edge(&mut self, edge_type: i32, source_name: &str, target_name: &str) {
        let resolver = self.resolver();
        let source_id = resolver.resolve_node_id(source_name);
        let target_id = resolver.resolve_node_id(target_name);
        if let (Some(src), Some(tgt)) = (source_id, target_id) {
            self.push_edge(edge_type, src, tgt);
        }
    }

    /// Qualified name of an ADT exactly as the definition pass emits it:
    /// its defining module's prefix + its name.
    fn adt_qualified_name(&self, adt: Adt) -> String {
        let prefix = self.module_prefix(adt.module(self.db));
        let name = adt.name(self.db).as_str().to_string();
        Self::qualify_in_module(&prefix, &name)
    }

    fn qualify_in_module(module_prefix: &str, name: &str) -> String {
        if module_prefix.is_empty() {
            return name.to_owned();
        }
        format!("{module_prefix}::{name}")
    }

    fn module_prefix(&self, module: ra_ap_hir::Module) -> String {
        let mut parts = Vec::new();
        let mut cursor = Some(module);
        while let Some(m) = cursor {
            if let Some(name) = m.name(self.db) {
                parts.push(name.as_str().to_string());
            }
            cursor = m.parent(self.db);
        }
        parts.reverse();
        parts.join("::")
    }

    /// The real (non-macro) vfs file that contains `in_file`'s original source.
    fn real_file_of<N: AstNode>(&self, in_file: &ra_ap_hir::InFile<N>) -> ra_ap_vfs::FileId {
        in_file.file_id.original_file(self.db).file_id(self.db)
    }

    /// Emit `NODE_TYPE_PARAMETER` nodes for all generic parameters (type,
    /// const, lifetime) of `gdef`, as members of `owner_name`, registered
    /// under their HIR identity for exact bound resolution.
    fn collect_generic_params(&mut self, owner_name: &str, gdef: ra_ap_hir::GenericDef) {
        for p in gdef.type_or_const_params(self.db) {
            let name = p.name(self.db).as_str().to_string();
            let Some(src) = p.source(self.db) else {
                continue;
            };
            // The implicit trait `Self` parameter has the trait as its source.
            let Either::Left(ast_param) = src.value.clone() else {
                continue;
            };
            let range = ast_param
                .name()
                .map(|n| n.syntax().text_range())
                .unwrap_or_else(|| ast_param.syntax().text_range());
            let qualified = format!("{owner_name}::{name}");
            if !self.node_ids.contains_key(&qualified) {
                self.emit_param_node(&qualified, src.file_id, range);
                self.add_edge(EDGE_MEMBER, owner_name, &qualified);
            }
            let key = match p.split(self.db) {
                Either::Left(cp) => DefKey::ConstParam(cp),
                Either::Right(tp) => DefKey::TypeParam(tp),
            };
            self.register_def(key, &qualified);
        }
        for lp in gdef.lifetime_params(self.db) {
            let name = lp.name(self.db).as_str().to_string();
            let Some(src) = lp.source(self.db) else {
                continue;
            };
            let range = src
                .value
                .lifetime()
                .map(|l| l.syntax().text_range())
                .unwrap_or_else(|| src.value.syntax().text_range());
            let qualified = format!("{owner_name}::{name}");
            if !self.node_ids.contains_key(&qualified) {
                self.emit_param_node(&qualified, src.file_id, range);
                self.add_edge(EDGE_MEMBER, owner_name, &qualified);
            }
            self.register_def(DefKey::LifetimeParam(lp), &qualified);
        }
    }

    /// Emit one NODE_TYPE_PARAMETER with its location mapped to real-file
    /// coordinates (macro-generated params become IMPLICIT).
    fn emit_param_node(
        &mut self,
        qualified: &str,
        file_id: ra_ap_hir::HirFileId,
        range: ra_ap_syntax::TextRange,
    ) {
        let is_macro = file_id.is_macro();
        let Some((vfs_fid, mapped)) = self.original_location(file_id, range) else {
            return;
        };
        self.add_def_kind(
            qualified,
            NODE_TYPE_PARAMETER,
            vfs_fid,
            mapped,
            if is_macro {
                DEFINITION_IMPLICIT
            } else {
                DEFINITION_EXPLICIT
            },
        );
    }

    fn collect_crate(&mut self, krate: Crate) {
        let mut reported_files: HashSet<String> = HashSet::new();
        for module in krate.modules(self.db) {
            let module_prefix = self.module_prefix(module);
            // Report per-file progress via the callback.
            {
                let def_src = module.definition_source(self.db);
                let editioned = def_src.file_id.original_file(self.db);
                let vfs_fid = editioned.file_id(self.db);
                if let Some(file_path) =
                    self.vfs.file_path(vfs_fid).as_path().map(|p| p.to_string())
                {
                    self.record_local_file(vfs_fid);
                    if reported_files.insert(file_path.clone()) {
                        (self.on_file)(&file_path);
                    }
                }
            }
            // declarations() covers most items but misses macro_rules! —
            // those only appear in the module scope as ScopeDef::ModuleDef(Macro).
            // Use scope() and deduplicate via a seen-name set.
            let mut seen = HashSet::new();
            for def in module.declarations(self.db) {
                if let Some(name) = def.name(self.db) {
                    seen.insert(name.as_str().to_string());
                }
                self.collect_module_def(&module_prefix, def);
            }
            // Pick up macro_rules! items not in declarations() (legacy textual macros).
            for m in module.legacy_macros(self.db) {
                let mname = m.name(self.db).as_str().to_string();
                if !seen.contains(&mname) {
                    seen.insert(mname.clone());
                    let qualified_name = Self::qualify_in_module(&module_prefix, &mname);
                    self.emit_from_source(m.source(self.db), &qualified_name, NODE_MACRO);
                    self.register_def(DefKey::Def(ModuleDef::Macro(m)), &qualified_name);
                }
            }
            // Collect parse errors for every file in this module.
            self.collect_parse_errors(module);
            // Walk impl blocks for EDGE_INHERITANCE, EDGE_TYPE_USAGE, and method collection.
            for imp in module.impl_defs(self.db) {
                self.collect_impl_items(&module_prefix, imp);
            }
        }
        // Reference edges (calls, inheritance, bounds, type arguments,
        // overrides) are emitted afterwards by the semantic reference pass
        // (collect_semantic_edges), once all crates' nodes are registered
        // in def_ids.
    }

    /// Associated items and supertraits are emitted via HIR; supertrait
    /// INHERITANCE edges themselves come from the semantic pass.
    fn collect_trait_details(&mut self, trait_name: &str, t: ra_ap_hir::Trait) {
        for item in t.items(self.db) {
            match item {
                AssocItem::Function(f) => {
                    let fname = f.name(self.db).as_str().to_string();
                    let qualified_name = format!("{trait_name}::{fname}");
                    self.emit_from_source(f.source(self.db), &qualified_name, NODE_METHOD);
                    self.register_def(DefKey::Def(ModuleDef::Function(f)), &qualified_name);
                    self.add_edge(EDGE_MEMBER, trait_name, &qualified_name);
                    self.collect_generic_params(&qualified_name, f.into());
                }
                AssocItem::TypeAlias(ta) => {
                    let taname = ta.name(self.db).as_str().to_string();
                    let qualified_name = format!("{trait_name}::{taname}");
                    self.emit_from_source(ta.source(self.db), &qualified_name, NODE_TYPEDEF);
                    self.register_def(DefKey::Def(ModuleDef::TypeAlias(ta)), &qualified_name);
                    self.add_edge(EDGE_MEMBER, trait_name, &qualified_name);
                }
                AssocItem::Const(c) => {
                    let Some(cname) = c.name(self.db) else {
                        continue;
                    };
                    let qualified_name = format!("{trait_name}::{}", cname.as_str());
                    self.emit_from_source(c.source(self.db), &qualified_name, NODE_GLOBAL_VARIABLE);
                    self.register_def(DefKey::Def(ModuleDef::Const(c)), &qualified_name);
                    self.add_edge(EDGE_MEMBER, trait_name, &qualified_name);
                }
            }
        }
    }

    fn collect_parse_errors(&mut self, module: ra_ap_hir::Module) {
        let Some(file_id) = module.as_source_file_id(self.db) else {
            return;
        };
        let errors = file_id.parse_errors(self.db);
        let Some(errors) = errors else { return };
        if errors.is_empty() {
            return;
        }
        let vfs_fid = file_id.file_id(self.db);
        let Some(file_path) = self.vfs_path(vfs_fid) else {
            return;
        };
        for err in errors.iter() {
            let err_id = self.alloc_id();
            self.storage.errors.push(OwnedStorageError {
                id: err_id,
                message: err.to_string(),
                translation_unit: file_path.clone(),
                fatal: false,
                indexed: true,
            });
        }
    }

    /// Collect HIR fields (struct/union/enum-variant). Handles both
    /// `FieldSource::Named` (named record field) and `FieldSource::Pos`
    /// (positional tuple field — no name token, use full syntax range).
    fn collect_hir_fields(&mut self, owner_name: &str, fields: Vec<ra_ap_hir::Field>) {
        for field in fields {
            let fname = field.name(self.db).as_str().to_string();
            let qualified_name = format!("{owner_name}::{fname}");
            let Some(field_src) = field.source(self.db) else {
                continue;
            };
            let range = match &field_src.value {
                FieldSource::Named(rf) => rf
                    .name()
                    .map(|n| n.syntax().text_range())
                    .unwrap_or_else(|| rf.syntax().text_range()),
                FieldSource::Pos(tf) => tf.syntax().text_range(),
            };
            let is_macro = field_src.file_id.is_macro();
            let Some((vfs_fid, mapped)) = self.original_location(field_src.file_id, range) else {
                continue;
            };
            self.add_def_kind(
                &qualified_name,
                NODE_FIELD,
                vfs_fid,
                mapped,
                if is_macro {
                    DEFINITION_IMPLICIT
                } else {
                    DEFINITION_EXPLICIT
                },
            );
            self.register_def(DefKey::Field(field), &qualified_name);
            self.add_edge(EDGE_MEMBER, owner_name, &qualified_name);
        }
    }

    fn collect_struct_fields(&mut self, struct_name: &str, s: ra_ap_hir::Struct) {
        self.collect_hir_fields(struct_name, s.fields(self.db));
    }

    fn collect_union_fields(&mut self, union_name: &str, u: ra_ap_hir::Union) {
        self.collect_hir_fields(union_name, u.fields(self.db));
    }

    fn collect_enum_variants(&mut self, enum_name: &str, e: ra_ap_hir::Enum) {
        for variant in e.variants(self.db) {
            let vname = variant.name(self.db).as_str().to_string();
            let qualified_variant = format!("{enum_name}::{vname}");
            self.emit_from_source(
                variant.source(self.db),
                &qualified_variant,
                NODE_ENUM_CONSTANT,
            );
            self.register_def(
                DefKey::Def(ModuleDef::EnumVariant(variant)),
                &qualified_variant,
            );
            self.add_edge(EDGE_MEMBER, enum_name, &qualified_variant);
            self.collect_hir_fields(&qualified_variant, variant.fields(self.db));
        }
    }

    fn collect_impl_items(&mut self, module_prefix: &str, imp: ra_ap_hir::Impl) {
        let src = imp.source(self.db);
        let owner_name = match &src {
            Some(src) => {
                let Some(self_ty_name) = impl_self_type_segment_name(&src.value) else {
                    return;
                };
                Self::qualify_in_module(module_prefix, &self_ty_name)
            }
            // Synthesized impl without AST (builtin derives like Clone/Debug):
            // derive the owner from the HIR self type.
            None => {
                let Some(adt) = imp.self_ty(self.db).as_adt() else {
                    return;
                };
                self.adt_qualified_name(adt)
            }
        };

        // The impl block's own generic parameters (`impl<T> …`) attach to the
        // self type's owner name; same-named params on the type definition
        // merge by serialized name on inject. (Synthesized impls have none.)
        if src.is_some() {
            self.collect_generic_params(&owner_name, imp.into());
        }

        for item in imp.items(self.db) {
            match item {
                AssocItem::Function(f) => {
                    let fname = f.name(self.db).as_str().to_string();
                    let qualified_name = format!("{owner_name}::{fname}");
                    let exists = self.node_ids.contains_key(&qualified_name);
                    if !exists {
                        // For synthesized derive methods, f.source() falls back
                        // to the TRAIT method's declaration (in the sysroot!) —
                        // use source_with_range, whose None-AST case carries the
                        // derive attribute range instead.
                        match f.source_with_range(self.db) {
                            Some(swr) if swr.value.1.is_some() => {
                                self.emit_from_source(
                                    f.source(self.db),
                                    &qualified_name,
                                    NODE_METHOD,
                                );
                                self.collect_generic_params(&qualified_name, f.into());
                                self.add_edge(EDGE_MEMBER, &owner_name, &qualified_name);
                            }
                            Some(swr) => {
                                let (range, _) = swr.value;
                                if let Some((vfs_fid, mapped)) =
                                    self.original_location(swr.file_id, range)
                                {
                                    self.add_def_kind(
                                        &qualified_name,
                                        NODE_METHOD,
                                        vfs_fid,
                                        mapped,
                                        DEFINITION_IMPLICIT,
                                    );
                                    self.add_edge(EDGE_MEMBER, &owner_name, &qualified_name);
                                }
                            }
                            None => {}
                        }
                    }
                    self.register_def(DefKey::Def(ModuleDef::Function(f)), &qualified_name);
                }
                AssocItem::Const(c) => {
                    if let Some(cname) = c.name(self.db) {
                        let cname = cname.as_str().to_string();
                        let qualified_name = format!("{owner_name}::{cname}");
                        let exists = self.node_ids.contains_key(&qualified_name);
                        if !exists {
                            self.emit_from_source(
                                c.source(self.db),
                                &qualified_name,
                                NODE_GLOBAL_VARIABLE,
                            );
                            self.add_edge(EDGE_MEMBER, &owner_name, &qualified_name);
                        }
                        self.register_def(DefKey::Def(ModuleDef::Const(c)), &qualified_name);
                    }
                }
                AssocItem::TypeAlias(ta) => {
                    let taname = ta.name(self.db).as_str().to_string();
                    let qualified_name = format!("{owner_name}::{taname}");
                    let exists = self.node_ids.contains_key(&qualified_name);
                    if !exists {
                        self.emit_from_source(ta.source(self.db), &qualified_name, NODE_TYPEDEF);
                        self.add_edge(EDGE_MEMBER, &owner_name, &qualified_name);
                    }
                    self.register_def(DefKey::Def(ModuleDef::TypeAlias(ta)), &qualified_name);
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Semantic reference pass
    // -----------------------------------------------------------------------

    /// Walk every collected local file and emit reference edges whose targets
    /// are resolved semantically (exact def identity), plus a TOKEN source
    /// location + occurrence per reference site, attached to the edge —
    /// mirroring the C++ side (ParserClientImpl::recordReference).
    ///
    /// The pass is split in two: pure per-file resolution
    /// (`RefResolver::resolve_file`) that fans out across worker threads for
    /// multi-file workspaces, and a single-threaded apply step that owns id
    /// allocation and edge dedup. Rows are applied strictly in `local_files`
    /// order, so the parallel path produces byte-identical output to the
    /// inline path. Set `RUST_INDEXER_SERIAL=1` to force the inline path —
    /// an operational escape hatch, and the reference for parity checks.
    fn collect_semantic_edges(&mut self, sema: &Semantics<'db, RootDatabase>) {
        let files: Vec<ra_ap_vfs::FileId> = self.local_files.clone();
        let workers = std::thread::available_parallelism()
            .map_or(1, |n| n.get())
            .min(files.len())
            .min(16);
        let force_serial = std::env::var_os("RUST_INDEXER_SERIAL").is_some_and(|v| v == "1");

        if files.len() < 2 || workers <= 1 || force_serial {
            for vfs_fid in files {
                // A panic inside rust-analyzer on one file must not lose the
                // references of all the others — record it as a non-fatal
                // error and continue with the next file.
                let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
                    self.resolver().resolve_file(sema, vfs_fid)
                }))
                .map_err(|payload| panic_payload_message(payload.as_ref()).to_owned());
                self.apply_file_result(vfs_fid, result);
            }
            return;
        }

        let results =
            resolve_files_parallel(self.db, &self.def_ids, &self.node_ids, &files, workers);
        for (vfs_fid, result) in files.into_iter().zip(results) {
            self.apply_file_result(vfs_fid, result);
        }
    }

    /// Apply one file's resolution outcome: either its reference rows or the
    /// panic marker of a failed resolution (recorded as a non-fatal error).
    fn apply_file_result(
        &mut self,
        vfs_fid: ra_ap_vfs::FileId,
        result: Result<Vec<ReferenceRow>, String>,
    ) {
        match result {
            Ok(rows) => {
                // Prime the per-file cache (allocating the file node, like
                // the pre-split per-file setup did); skip files that do not
                // resolve to a path.
                if self.file_info(vfs_fid).is_none() {
                    return;
                }
                self.apply_reference_rows(vfs_fid, rows);
            }
            Err(panic_message) => {
                let file_path = self
                    .vfs_path(vfs_fid)
                    .unwrap_or_else(|| "<unknown file>".to_owned());
                let error = OwnedStorageError {
                    id: self.alloc_id(),
                    message: format!(
                        "indexer panicked while resolving references in {file_path}: {panic_message}"
                    ),
                    translation_unit: file_path,
                    fatal: false,
                    indexed: true,
                };
                self.storage.errors.push(error);
            }
        }
    }

    /// The single-threaded half of the reference pass: edge dedup via
    /// `push_edge` plus the occurrence location per row. Id allocation and
    /// `emitted_edges` dedup live here and must never move into the workers —
    /// apply order (= `local_files` order) defines the deterministic output.
    fn apply_reference_rows(&mut self, vfs_fid: ra_ap_vfs::FileId, rows: Vec<ReferenceRow>) {
        for row in rows {
            let edge_id = self.push_edge(row.edge_type, row.source_id, row.target_id);
            if let Some(range) = row.range {
                self.add_reference_location(edge_id, range, vfs_fid);
            }
        }
    }

    /// Record a TOKEN source location + occurrence for `element_id` (an edge).
    /// Reads the cached `FileInfo` directly (the reference pass hits this once
    /// per site) and copies the plain values out before pushing storage rows.
    fn add_reference_location(
        &mut self,
        element_id: i64,
        range: ra_ap_syntax::TextRange,
        vfs_fid: ra_ap_vfs::FileId,
    ) {
        let loc_id = self.alloc_id();
        let Some(info) = self.file_info(vfs_fid) else {
            return;
        };
        let file_node_id = info.file_node_id;
        let (Some(start), Some(end)) = (
            info.line_index.try_line_col(range.start()),
            info.line_index.try_line_col(range.end()),
        ) else {
            return;
        };
        self.storage
            .source_locations
            .push(OwnedStorageSourceLocation {
                id: loc_id,
                file_node_id,
                start_line: start.line + 1,
                start_col: start.col + 1,
                end_line: end.line + 1,
                end_col: end.col + 1,
                type_: LOCATION_TOKEN,
            });
        self.storage.occurrences.push(OwnedStorageOccurrence {
            element_id,
            source_location_id: loc_id,
        });
    }

    fn collect_module_def(&mut self, module_prefix: &str, def: ModuleDef) {
        match def {
            ModuleDef::Function(f) => {
                let name = f.name(self.db).as_str().to_string();
                let qualified_name = Self::qualify_in_module(module_prefix, &name);
                let kind = if f.as_assoc_item(self.db).is_some() {
                    NODE_METHOD
                } else {
                    NODE_FUNCTION
                };
                let src = f.source(self.db);
                self.emit_from_source(src, &qualified_name, kind);
                self.register_def(DefKey::Def(def), &qualified_name);
                self.collect_generic_params(&qualified_name, f.into());
            }
            ModuleDef::Adt(adt) => match adt {
                Adt::Struct(s) => {
                    let name = s.name(self.db).as_str().to_string();
                    let qualified_name = Self::qualify_in_module(module_prefix, &name);
                    let src = s.source(self.db);
                    self.emit_from_source(src, &qualified_name, NODE_STRUCT);
                    self.register_def(DefKey::Def(def), &qualified_name);
                    self.collect_generic_params(&qualified_name, s.into());
                    self.collect_struct_fields(&qualified_name, s);
                }
                Adt::Enum(e) => {
                    let name = e.name(self.db).as_str().to_string();
                    let qualified_name = Self::qualify_in_module(module_prefix, &name);
                    let src = e.source(self.db);
                    self.emit_from_source(src, &qualified_name, NODE_ENUM);
                    self.register_def(DefKey::Def(def), &qualified_name);
                    self.collect_generic_params(&qualified_name, e.into());
                    self.collect_enum_variants(&qualified_name, e);
                }
                Adt::Union(u) => {
                    let name = u.name(self.db).as_str().to_string();
                    let qualified_name = Self::qualify_in_module(module_prefix, &name);
                    let src = u.source(self.db);
                    self.emit_from_source(src, &qualified_name, NODE_UNION);
                    self.register_def(DefKey::Def(def), &qualified_name);
                    self.collect_generic_params(&qualified_name, u.into());
                    self.collect_union_fields(&qualified_name, u);
                }
            },
            ModuleDef::Trait(t) => {
                let name = t.name(self.db).as_str().to_string();
                let qualified_name = Self::qualify_in_module(module_prefix, &name);
                let src = t.source(self.db);
                self.emit_from_source(src, &qualified_name, NODE_INTERFACE);
                self.register_def(DefKey::Def(def), &qualified_name);
                self.collect_generic_params(&qualified_name, t.into());
                self.collect_trait_details(&qualified_name, t);
            }
            ModuleDef::TypeAlias(ta) => {
                let name = ta.name(self.db).as_str().to_string();
                let qualified_name = Self::qualify_in_module(module_prefix, &name);
                let src = ta.source(self.db);
                self.emit_from_source(src, &qualified_name, NODE_TYPEDEF);
                self.register_def(DefKey::Def(def), &qualified_name);
                self.collect_generic_params(&qualified_name, ta.into());
            }
            ModuleDef::Const(c) => {
                if let Some(name) = c.name(self.db) {
                    let name = name.as_str().to_string();
                    let qualified_name = Self::qualify_in_module(module_prefix, &name);
                    self.emit_from_source(c.source(self.db), &qualified_name, NODE_GLOBAL_VARIABLE);
                    self.register_def(DefKey::Def(def), &qualified_name);
                }
            }
            ModuleDef::Static(s) => {
                let name = s.name(self.db).as_str().to_string();
                let qualified_name = Self::qualify_in_module(module_prefix, &name);
                self.emit_from_source(s.source(self.db), &qualified_name, NODE_GLOBAL_VARIABLE);
                self.register_def(DefKey::Def(def), &qualified_name);
            }
            ModuleDef::Macro(m) => {
                let name = m.name(self.db).as_str().to_string();
                let qualified_name = Self::qualify_in_module(module_prefix, &name);
                self.emit_from_source(m.source(self.db), &qualified_name, NODE_MACRO);
                self.register_def(DefKey::Def(def), &qualified_name);
            }
            ModuleDef::Module(m) => {
                if let Some(name) = m.name(self.db) {
                    let name = name.as_str().to_string();
                    let qualified_name = Self::qualify_in_module(module_prefix, &name);
                    // declaration_source gives the `mod foo;` node (inline modules
                    // have no declaration node — they are covered by their parent).
                    self.emit_module_decl(m, &qualified_name);
                    self.register_def(DefKey::Def(def), &qualified_name);
                }
            }
            ModuleDef::BuiltinType(_) | ModuleDef::EnumVariant(_) => {}
        }
    }

    fn emit_module_decl(&mut self, m: ra_ap_hir::Module, name: &str) {
        // Use declaration_source (the `mod foo;` item) when available.
        // For inline modules (`mod foo { … }`) declaration_source is None;
        // fall back to definition_source_range to still record a location.
        if let Some(decl) = m.declaration_source(self.db) {
            self.emit_from_source(Some(decl), name, NODE_MODULE);
        } else {
            let range_in_file = m.definition_source_range(self.db);
            let is_macro = range_in_file.file_id.is_macro();
            let Some((vfs_fid, mapped)) =
                self.original_location(range_in_file.file_id, range_in_file.value)
            else {
                return;
            };
            self.add_def_kind(
                name,
                NODE_MODULE,
                vfs_fid,
                mapped,
                if is_macro {
                    DEFINITION_IMPLICIT
                } else {
                    DEFINITION_EXPLICIT
                },
            );
        }
    }

    /// Generic helper: given an `InFile<ast::*>` source, extract the name
    /// token range and emit a node, plus a SCOPE location spanning the whole
    /// item (drives snippet extents in the code view).
    ///
    /// Macro-generated items (source in an expansion) are emitted as
    /// DefinitionKind::IMPLICIT with their location mapped to the real file
    /// (falling back to the macro call site); no scope location.
    fn emit_from_source<N: AstNode + HasName>(
        &mut self,
        src: Option<ra_ap_hir::InFile<N>>,
        name: &str,
        kind: i32,
    ) {
        let Some(in_file) = src else { return };
        let full_range = in_file.value.syntax().text_range();
        let range = in_file
            .value
            .name()
            .map(|n| n.syntax().text_range())
            .unwrap_or(full_range);
        if in_file.file_id.is_macro() {
            let Some((vfs_fid, mapped)) = self.original_location(in_file.file_id, range) else {
                return;
            };
            self.add_def_kind(name, kind, vfs_fid, mapped, DEFINITION_IMPLICIT);
            return;
        }
        let vfs_fid = self.real_file_of(&in_file);
        self.add_def(name, kind, vfs_fid, range);
        if full_range != range {
            self.add_scope_location(name, full_range, vfs_fid);
        }
    }

    /// Record a SCOPE location for the already-emitted node `name`.
    fn add_scope_location(
        &mut self,
        name: &str,
        range: ra_ap_syntax::TextRange,
        vfs_file_id: ra_ap_vfs::FileId,
    ) {
        let Some(&node_id) = self.node_ids.get(name) else {
            return;
        };
        let loc_id = self.alloc_id();
        let Some((file_node_id, start, end)) = self.location_of(vfs_file_id, range) else {
            return;
        };
        self.storage
            .source_locations
            .push(OwnedStorageSourceLocation {
                id: loc_id,
                file_node_id,
                start_line: start.line + 1,
                start_col: start.col + 1,
                end_line: end.line + 1,
                end_col: end.col + 1,
                type_: LOCATION_SCOPE,
            });
        self.storage.occurrences.push(OwnedStorageOccurrence {
            element_id: node_id,
            source_location_id: loc_id,
        });
    }

    /// Emit a node from a raw `InFile<N>` without requiring `HasName` — uses
    /// the whole syntax node range. Used for items that don't implement HasName
    /// (e.g. macro_rules! via legacy_macros, module decls).
    #[expect(
        dead_code,
        reason = "staged for indexing name-less items (macro invocations)"
    )]
    fn emit_from_source_no_name<N: AstNode>(
        &mut self,
        src: Option<ra_ap_hir::InFile<N>>,
        name: &str,
        kind: i32,
    ) {
        let Some(in_file) = src else { return };
        let vfs_fid = self.real_file_of(&in_file);
        let range = in_file.value.syntax().text_range();
        self.add_def(name, kind, vfs_fid, range);
    }
}

/// Extract a human-readable message from a `catch_unwind` payload — panics
/// carry a `&str` (literal message) or a `String` (formatted message).
fn panic_payload_message(payload: &(dyn std::any::Any + Send)) -> &str {
    if let Some(s) = payload.downcast_ref::<&str>() {
        s
    } else if let Some(s) = payload.downcast_ref::<String>() {
        s
    } else {
        "non-string panic payload"
    }
}

/// Map one generic-item node (fn / struct / enum / union / trait / type alias
/// / impl) to its HIR `GenericDef`, if the node resolves semantically.
fn generic_def_of(
    sema: &Semantics<'_, RootDatabase>,
    node: &ra_ap_syntax::SyntaxNode,
) -> Option<ra_ap_hir::GenericDef> {
    match node.kind() {
        SyntaxKind::FN => ast::Fn::cast(node.clone())
            .and_then(|it| sema.to_def(&it))
            .map(Into::into),
        SyntaxKind::STRUCT => ast::Struct::cast(node.clone())
            .and_then(|it| sema.to_def(&it))
            .map(|s| Adt::Struct(s).into()),
        SyntaxKind::ENUM => ast::Enum::cast(node.clone())
            .and_then(|it| sema.to_def(&it))
            .map(|e| Adt::Enum(e).into()),
        SyntaxKind::UNION => ast::Union::cast(node.clone())
            .and_then(|it| sema.to_def(&it))
            .map(|u| Adt::Union(u).into()),
        SyntaxKind::TRAIT => ast::Trait::cast(node.clone())
            .and_then(|it| sema.to_def(&it))
            .map(Into::into),
        SyntaxKind::TYPE_ALIAS => ast::TypeAlias::cast(node.clone())
            .and_then(|it| sema.to_def(&it))
            .map(Into::into),
        SyntaxKind::IMPL => ast::Impl::cast(node.clone())
            .and_then(|it| sema.to_def(&it))
            .map(Into::into),
        _ => None,
    }
}

impl RefResolver<'_> {
    fn def_id(&self, key: DefKey) -> Option<i64> {
        self.def_ids.get(&key).copied()
    }

    /// Resolve a plain name to an emitted node id: exact qualified-name hit
    /// first, then a unique `::name` suffix match.
    fn resolve_node_id(&self, query: &str) -> Option<i64> {
        if let Some(&id) = self.node_ids.get(query) {
            return Some(id);
        }

        let suffix = format!("::{query}");
        let mut matches = self
            .node_ids
            .iter()
            .filter(|(k, _)| k.ends_with(&suffix))
            .map(|(_, &v)| v);

        let first = matches.next()?;
        if matches.next().is_some() {
            return None;
        }
        Some(first)
    }

    /// Resolve every reference site in `file_id` into rows, in syntax-tree
    /// order. Pure: reads the database and the definition maps, allocates no
    /// ids and dedups nothing — that is the apply step's job. Falls back to
    /// name-based lookup (`resolve_node_id`) per site when semantic
    /// resolution fails, e.g. inside unexpanded macros.
    fn resolve_file(
        &self,
        sema: &Semantics<'_, RootDatabase>,
        file_id: ra_ap_vfs::FileId,
    ) -> Vec<ReferenceRow> {
        let mut rows = Vec::new();
        let source_file = sema.parse_guess_edition(file_id);
        for node in source_file.syntax().descendants() {
            match node.kind() {
                SyntaxKind::IMPL => {
                    if let Some(impl_ast) = ast::Impl::cast(node) {
                        self.resolve_impl_inheritance(sema, &impl_ast, &mut rows);
                        self.resolve_impl_overrides(sema, &impl_ast, &mut rows);
                    }
                }
                SyntaxKind::LIFETIME => {
                    self.resolve_lifetime_bound_reference(sema, &node, &mut rows);
                }
                SyntaxKind::CALL_EXPR => {
                    let Some(call) = ast::CallExpr::cast(node) else {
                        continue;
                    };
                    let Some(ast::Expr::PathExpr(pe)) = call.expr() else {
                        continue;
                    };
                    let Some(path) = pe.path() else { continue };
                    let target = match sema.resolve_path(&path) {
                        Some(PathResolution::Def(def)) => self.def_id(DefKey::Def(def)),
                        _ => None,
                    }
                    .or_else(|| {
                        let name = path.segment()?.name_ref()?.text().to_string();
                        self.resolve_node_id(&name)
                    });
                    let range = path_name_range(&path);
                    self.resolve_reference(
                        sema,
                        EDGE_CALL,
                        call.syntax(),
                        target,
                        range,
                        &mut rows,
                    );
                }
                SyntaxKind::METHOD_CALL_EXPR => {
                    let Some(call) = ast::MethodCallExpr::cast(node) else {
                        continue;
                    };
                    let target = sema
                        .resolve_method_call(&call)
                        .and_then(|f| self.def_id(DefKey::Def(ModuleDef::Function(f))))
                        .or_else(|| {
                            let name = call.name_ref()?.text().to_string();
                            self.resolve_node_id(&name)
                        });
                    let range = call.name_ref().map(|n| n.syntax().text_range());
                    self.resolve_reference(
                        sema,
                        EDGE_CALL,
                        call.syntax(),
                        target,
                        range,
                        &mut rows,
                    );
                }
                SyntaxKind::PATH_EXPR => {
                    // Callees are handled by the CALL_EXPR arm.
                    if node
                        .parent()
                        .is_some_and(|p| p.kind() == SyntaxKind::CALL_EXPR)
                    {
                        continue;
                    }
                    let Some(pe) = ast::PathExpr::cast(node) else {
                        continue;
                    };
                    let Some(path) = pe.path() else { continue };
                    // Only definition references; locals are future work.
                    let Some(PathResolution::Def(def)) = sema.resolve_path(&path) else {
                        continue;
                    };
                    let target = self.def_id(DefKey::Def(def));
                    let range = path_name_range(&path);
                    self.resolve_reference(sema, EDGE_USAGE, pe.syntax(), target, range, &mut rows);
                }
                SyntaxKind::PATH_TYPE => {
                    let Some(pt) = ast::PathType::cast(node) else {
                        continue;
                    };
                    self.resolve_type_reference(sema, &pt, &mut rows);
                }
                SyntaxKind::FIELD_EXPR => {
                    let Some(fe) = ast::FieldExpr::cast(node) else {
                        continue;
                    };
                    let target = match sema.resolve_field(&fe) {
                        Some(Either::Left(field)) => self.def_id(DefKey::Field(field)),
                        _ => None,
                    };
                    let range = fe.name_ref().map(|n| n.syntax().text_range());
                    self.resolve_reference(sema, EDGE_USAGE, fe.syntax(), target, range, &mut rows);
                }
                SyntaxKind::RECORD_EXPR_FIELD => {
                    let Some(ref_field) = ast::RecordExprField::cast(node) else {
                        continue;
                    };
                    let target = sema
                        .resolve_record_field(&ref_field)
                        .and_then(|(field, _, _)| self.def_id(DefKey::Field(field)));
                    let range = ref_field.name_ref().map(|n| n.syntax().text_range());
                    self.resolve_reference(
                        sema,
                        EDGE_USAGE,
                        ref_field.syntax(),
                        target,
                        range,
                        &mut rows,
                    );
                }
                _ => {}
            }
        }
        rows
    }

    /// Type reference (`PathType`) → edge from the bound owner or enclosing
    /// item (see context/DESIGN_RUST_TYPE_SYSTEM_EDGES.md):
    ///   - inside a generic-param bound: from the *parameter* node
    ///   - supertrait bound: EDGE_INHERITANCE from the trait
    ///   - inside a generic-arg list: EDGE_TYPE_ARGUMENT instead of TYPE_USAGE
    ///   - plain type position: EDGE_TYPE_USAGE from the enclosing item;
    ///     impl headers are skipped (covered by inheritance handling).
    fn resolve_type_reference(
        &self,
        sema: &Semantics<'_, RootDatabase>,
        pt: &ast::PathType,
        rows: &mut Vec<ReferenceRow>,
    ) {
        let Some(path) = pt.path() else { return };
        let target = match sema.resolve_path(&path) {
            Some(PathResolution::Def(def)) => self.def_id(DefKey::Def(def)),
            Some(PathResolution::TypeParam(tp)) => self.def_id(DefKey::TypeParam(tp)),
            Some(PathResolution::ConstParam(cp)) => self.def_id(DefKey::ConstParam(cp)),
            _ => None,
        };
        let Some(target) = target else { return };
        let range = path_name_range(&path);

        let in_generic_args = pt
            .syntax()
            .ancestors()
            .any(|a| a.kind() == SyntaxKind::GENERIC_ARG_LIST);
        let edge_kind = if in_generic_args {
            EDGE_TYPE_ARGUMENT
        } else {
            EDGE_TYPE_USAGE
        };

        let source = match self.bound_owner(sema, pt.syntax()) {
            BoundOwner::Param(param_id) => Some(param_id),
            // `trait A: B` — the top-level bound path is the supertrait.
            BoundOwner::Trait(trait_id) if !in_generic_args => {
                if trait_id != target {
                    rows.push(ReferenceRow {
                        edge_type: EDGE_INHERITANCE,
                        source_id: trait_id,
                        target_id: target,
                        range,
                    });
                }
                return;
            }
            BoundOwner::Trait(trait_id) => Some(trait_id),
            BoundOwner::Item => {
                self.enclosing_context_id(sema, pt.syntax(), /*allow_impl_context=*/ false)
            }
        };
        let Some(source) = source else { return };
        if source == target {
            return;
        }
        rows.push(ReferenceRow {
            edge_type: edge_kind,
            source_id: source,
            target_id: target,
            range,
        });
    }

    /// Classify the bound context of a node inside a `TypeBoundList`.
    fn bound_owner(
        &self,
        sema: &Semantics<'_, RootDatabase>,
        node: &ra_ap_syntax::SyntaxNode,
    ) -> BoundOwner {
        let Some(bound_list) = node
            .ancestors()
            .find(|a| a.kind() == SyntaxKind::TYPE_BOUND_LIST)
        else {
            return BoundOwner::Item;
        };
        let Some(parent) = bound_list.parent() else {
            return BoundOwner::Item;
        };
        match parent.kind() {
            SyntaxKind::TYPE_PARAM => ast::TypeParam::cast(parent)
                .and_then(|tp| sema.to_def(&tp))
                .and_then(|tp| self.def_id(DefKey::TypeParam(tp)))
                .map_or(BoundOwner::Item, BoundOwner::Param),
            SyntaxKind::LIFETIME_PARAM => ast::LifetimeParam::cast(parent)
                .and_then(|lp| sema.to_def(&lp))
                .and_then(|lp| self.def_id(DefKey::LifetimeParam(lp)))
                .map_or(BoundOwner::Item, BoundOwner::Param),
            SyntaxKind::WHERE_PRED => {
                let Some(pred) = ast::WherePred::cast(parent) else {
                    return BoundOwner::Item;
                };
                // `where T: Trait` — attach to T if the subject is a parameter.
                if let Some(ast::Type::PathType(subject)) = pred.ty() {
                    if let Some(path) = subject.path() {
                        match sema.resolve_path(&path) {
                            Some(PathResolution::TypeParam(tp)) => {
                                return self
                                    .def_id(DefKey::TypeParam(tp))
                                    .map_or(BoundOwner::Item, BoundOwner::Param);
                            }
                            Some(PathResolution::ConstParam(cp)) => {
                                return self
                                    .def_id(DefKey::ConstParam(cp))
                                    .map_or(BoundOwner::Item, BoundOwner::Param);
                            }
                            _ => {}
                        }
                    }
                }
                // `where 'a: 'b` — attach to 'a.
                if let Some(lt) = pred.lifetime() {
                    if let Some(id) = self.lifetime_param_id(sema, pred.syntax(), &lt.text()) {
                        return BoundOwner::Param(id);
                    }
                }
                BoundOwner::Item
            }
            SyntaxKind::TRAIT => ast::Trait::cast(parent)
                .and_then(|t| sema.to_def(&t))
                .and_then(|t| self.def_id(DefKey::Def(ModuleDef::Trait(t))))
                .map_or(BoundOwner::Item, BoundOwner::Trait),
            // Associated type bounds (`type Item: Debug`) attach to the alias.
            SyntaxKind::TYPE_ALIAS => ast::TypeAlias::cast(parent)
                .and_then(|ta| sema.to_def(&ta))
                .and_then(|ta| self.def_id(DefKey::Def(ModuleDef::TypeAlias(ta))))
                .map_or(BoundOwner::Item, BoundOwner::Param),
            // `dyn Trait` / `impl Trait` positions are ordinary type usage.
            _ => BoundOwner::Item,
        }
    }

    /// Lifetime reference in a bound position (`'a: 'b`, `T: 'a`) →
    /// EDGE_TYPE_USAGE from the bound owner to the lifetime parameter node.
    /// Declaration sites and plain uses in types are not recorded (v1).
    fn resolve_lifetime_bound_reference(
        &self,
        sema: &Semantics<'_, RootDatabase>,
        node: &ra_ap_syntax::SyntaxNode,
        rows: &mut Vec<ReferenceRow>,
    ) {
        // Only lifetimes that are themselves a bound (inside a TypeBoundList).
        if !node
            .ancestors()
            .skip(1)
            .take_while(|a| a.kind() != SyntaxKind::GENERIC_PARAM_LIST)
            .any(|a| a.kind() == SyntaxKind::TYPE_BOUND_LIST)
        {
            return;
        }
        let name = node.text().to_string();
        let Some(target) = self.lifetime_param_id(sema, node, &name) else {
            return;
        };
        let source = match self.bound_owner(sema, node) {
            BoundOwner::Param(id) => Some(id),
            BoundOwner::Trait(id) => Some(id),
            BoundOwner::Item => self.enclosing_context_id(sema, node, false),
        };
        let Some(source) = source else { return };
        if source == target {
            return;
        }
        rows.push(ReferenceRow {
            edge_type: EDGE_TYPE_USAGE,
            source_id: source,
            target_id: target,
            range: Some(node.text_range()),
        });
    }

    /// Resolve a lifetime name (`'a`) to its parameter node by walking the
    /// enclosing generic items and matching their `lifetime_params` by name.
    fn lifetime_param_id(
        &self,
        sema: &Semantics<'_, RootDatabase>,
        node: &ra_ap_syntax::SyntaxNode,
        name: &str,
    ) -> Option<i64> {
        for anc in node.ancestors() {
            let Some(gdef) = generic_def_of(sema, &anc) else {
                continue;
            };
            let hit = gdef
                .lifetime_params(self.db)
                .into_iter()
                .find(|lp| lp.name(self.db).as_str() == name)
                .and_then(|lp| self.def_id(DefKey::LifetimeParam(lp)));
            if hit.is_some() {
                return hit;
            }
        }
        None
    }

    /// `impl Trait for Type` — every impl function overrides its trait
    /// counterpart (matched by name, guaranteed by the language):
    ///   Type::method —override→ Trait::method
    fn resolve_impl_overrides(
        &self,
        sema: &Semantics<'_, RootDatabase>,
        impl_ast: &ast::Impl,
        rows: &mut Vec<ReferenceRow>,
    ) {
        let Some(hir_impl) = sema.to_def(impl_ast) else {
            return;
        };
        let Some(trait_) = hir_impl.trait_(self.db) else {
            return;
        };
        for item in hir_impl.items(self.db) {
            let AssocItem::Function(f) = item else {
                continue;
            };
            let Some(impl_fn_id) = self.def_id(DefKey::Def(ModuleDef::Function(f))) else {
                continue;
            };
            let fname = f.name(self.db);
            let trait_fn_id = trait_.items(self.db).into_iter().find_map(|ti| {
                if let AssocItem::Function(tf) = ti {
                    if tf.name(self.db) == fname {
                        return self.def_id(DefKey::Def(ModuleDef::Function(tf)));
                    }
                }
                None
            });
            if let Some(trait_fn_id) = trait_fn_id {
                rows.push(ReferenceRow {
                    edge_type: EDGE_OVERRIDE,
                    source_id: impl_fn_id,
                    target_id: trait_fn_id,
                    range: None,
                });
            }
        }
    }

    /// Reference row from the enclosing context of `ref_site` to `target`,
    /// with a TOKEN occurrence at `range`.
    fn resolve_reference(
        &self,
        sema: &Semantics<'_, RootDatabase>,
        edge_type: i32,
        ref_site: &ra_ap_syntax::SyntaxNode,
        target: Option<i64>,
        range: Option<ra_ap_syntax::TextRange>,
        rows: &mut Vec<ReferenceRow>,
    ) {
        let Some(target) = target else { return };
        let Some(context) =
            self.enclosing_context_id(sema, ref_site, /*allow_impl_context=*/ true)
        else {
            return;
        };
        if context == target {
            return;
        }
        rows.push(ReferenceRow {
            edge_type,
            source_id: context,
            target_id: target,
            range,
        });
    }

    /// `impl Trait for Type` → EDGE_INHERITANCE from Type to Trait.
    /// Trait and self type are resolved via `Semantics::resolve_path`; each side
    /// independently falls back to name lookup on the path segment text.
    fn resolve_impl_inheritance(
        &self,
        sema: &Semantics<'_, RootDatabase>,
        impl_ast: &ast::Impl,
        rows: &mut Vec<ReferenceRow>,
    ) {
        // Only `impl Trait for Type` produces an inheritance edge.
        if impl_ast.for_token().is_none() {
            return;
        }
        let type_nodes: Vec<ast::Type> = impl_ast
            .syntax()
            .children()
            .filter_map(ast::Type::cast)
            .collect();
        if type_nodes.len() < 2 {
            return;
        }
        let trait_id = self.resolve_type_node(sema, &type_nodes[0]);
        let self_id = self.resolve_type_node(sema, &type_nodes[1]);
        if let (Some(self_id), Some(trait_id)) = (self_id, trait_id) {
            let range = if let ast::Type::PathType(pt) = &type_nodes[0] {
                pt.path().as_ref().and_then(path_name_range)
            } else {
                None
            };
            rows.push(ReferenceRow {
                edge_type: EDGE_INHERITANCE,
                source_id: self_id,
                target_id: trait_id,
                range,
            });
        }
    }

    /// Resolve a path-shaped AST type to an emitted node id: semantically first,
    /// then by segment-name fallback.
    fn resolve_type_node(&self, sema: &Semantics<'_, RootDatabase>, ty: &ast::Type) -> Option<i64> {
        let ast::Type::PathType(pt) = ty else {
            return None;
        };
        let path = pt.path()?;
        match sema.resolve_path(&path) {
            Some(PathResolution::Def(def)) => self.def_id(DefKey::Def(def)),
            _ => None,
        }
        .or_else(|| {
            let name = path.segment()?.name_ref()?.text().to_string();
            self.resolve_node_id(&name)
        })
    }

    /// Node id of the nearest enclosing emitted item of `node` — the reference
    /// context. Walks outwards through functions, ADTs, traits, type aliases,
    /// consts and statics; an `impl` header (reached without hitting an assoc
    /// item first) yields the impl's self type when `allow_impl_context`,
    /// otherwise no context.
    fn enclosing_context_id(
        &self,
        sema: &Semantics<'_, RootDatabase>,
        node: &ra_ap_syntax::SyntaxNode,
        allow_impl_context: bool,
    ) -> Option<i64> {
        for anc in node.ancestors().skip(1) {
            let candidate = match anc.kind() {
                SyntaxKind::FN => ast::Fn::cast(anc)
                    .and_then(|it| sema.to_def(&it))
                    .and_then(|f| self.def_id(DefKey::Def(ModuleDef::Function(f)))),
                SyntaxKind::STRUCT => ast::Struct::cast(anc)
                    .and_then(|it| sema.to_def(&it))
                    .and_then(|s| self.def_id(DefKey::Def(ModuleDef::Adt(Adt::Struct(s))))),
                SyntaxKind::ENUM => ast::Enum::cast(anc)
                    .and_then(|it| sema.to_def(&it))
                    .and_then(|e| self.def_id(DefKey::Def(ModuleDef::Adt(Adt::Enum(e))))),
                SyntaxKind::UNION => ast::Union::cast(anc)
                    .and_then(|it| sema.to_def(&it))
                    .and_then(|u| self.def_id(DefKey::Def(ModuleDef::Adt(Adt::Union(u))))),
                SyntaxKind::TRAIT => ast::Trait::cast(anc)
                    .and_then(|it| sema.to_def(&it))
                    .and_then(|t| self.def_id(DefKey::Def(ModuleDef::Trait(t)))),
                SyntaxKind::TYPE_ALIAS => ast::TypeAlias::cast(anc)
                    .and_then(|it| sema.to_def(&it))
                    .and_then(|ta| self.def_id(DefKey::Def(ModuleDef::TypeAlias(ta)))),
                SyntaxKind::CONST => ast::Const::cast(anc)
                    .and_then(|it| sema.to_def(&it))
                    .and_then(|c| self.def_id(DefKey::Def(ModuleDef::Const(c)))),
                SyntaxKind::STATIC => ast::Static::cast(anc)
                    .and_then(|it| sema.to_def(&it))
                    .and_then(|s| self.def_id(DefKey::Def(ModuleDef::Static(s)))),
                SyntaxKind::IMPL => {
                    if !allow_impl_context {
                        return None;
                    }
                    ast::Impl::cast(anc)
                        .and_then(|it| sema.to_def(&it))
                        .and_then(|i| i.self_ty(self.db).as_adt())
                        .and_then(|adt| self.def_id(DefKey::Def(ModuleDef::Adt(adt))))
                }
                _ => continue,
            };
            if candidate.is_some() {
                return candidate;
            }
            // Item ancestor found but not emitted (e.g. a nested fn) — keep
            // walking outwards to attribute the reference to its parent.
        }
        None
    }
}

/// Fan the resolution step out across `workers` scoped threads. Each worker
/// owns a cheap clone of the database (a handle onto the same salsa storage,
/// so the ids inside `def_ids` stay valid) and attaches it to its thread —
/// the next-trait-solver keeps per-thread database TLS, so `attach_db` is
/// mandatory before any type queries run on a worker. Files are pulled off a
/// shared counter; results come back per file as rows or a panic message and
/// are returned in `files` order for the deterministic apply step.
fn resolve_files_parallel(
    db: &RootDatabase,
    def_ids: &HashMap<DefKey, i64>,
    node_ids: &HashMap<String, i64>,
    files: &[ra_ap_vfs::FileId],
    workers: usize,
) -> Vec<Result<Vec<ReferenceRow>, String>> {
    let next_file = AtomicUsize::new(0);
    let (tx, rx) = std::sync::mpsc::channel();
    std::thread::scope(|scope| {
        for _ in 0..workers {
            let worker_db = db.clone();
            let tx = tx.clone();
            let next_file = &next_file;
            scope.spawn(move || {
                ra_ap_hir::attach_db(&worker_db, || {
                    let sema = Semantics::new(&worker_db);
                    let resolver = RefResolver {
                        db: &worker_db,
                        def_ids,
                        node_ids,
                    };
                    loop {
                        let index = next_file.fetch_add(1, Ordering::Relaxed);
                        let Some(&file_id) = files.get(index) else {
                            break;
                        };
                        let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
                            resolver.resolve_file(&sema, file_id)
                        }))
                        .map_err(|payload| panic_payload_message(payload.as_ref()).to_owned());
                        if tx.send((index, result)).is_err() {
                            break;
                        }
                    }
                });
            });
        }
        drop(tx);
    });
    // All workers have joined; drain the buffered results into file order.
    let mut slots: Vec<Option<Result<Vec<ReferenceRow>, String>>> =
        files.iter().map(|_| None).collect();
    for (index, result) in rx {
        slots[index] = Some(result);
    }
    slots
        .into_iter()
        .map(|slot| slot.unwrap_or_else(|| Err("file skipped by all workers".to_owned())))
        .collect()
}

pub(super) fn collect_from_db<'db>(
    db: &'db RootDatabase,
    vfs: &'db Vfs,
    on_file: impl FnMut(&str) + 'db,
) -> OwnedIntermediateStorage {
    // HIR type inference (Semantics::resolve_method_call, Impl::self_ty, …)
    // runs on the next-trait-solver, which requires the database to be
    // attached to the current thread via hir_ty's TLS.
    ra_ap_hir::attach_db(db, || {
        let mut collector = Collector::with_callback(db, vfs, on_file);

        for krate in Crate::all(db) {
            // Skip library crates (std, core, registry deps) — only index
            // local crates. Lang crates are identified by origin; registry
            // and toolchain crates by path as a second net.
            if matches!(
                krate.origin(db),
                ra_ap_ide_db::base_db::CrateOrigin::Lang(_)
            ) {
                continue;
            }
            let vfs_fid = krate.root_file(db);
            let Some(crate_root_path) = vfs.file_path(vfs_fid).as_path().map(|p| p.to_string())
            else {
                continue;
            };
            let is_local = !crate_root_path.contains("/.cargo/")
                && !crate_root_path.contains("/.rustup/")
                && !crate_root_path.contains("/rustup/")
                && !crate_root_path.contains("/lib/rustlib/");
            if !is_local {
                continue;
            }
            // A panic inside rust-analyzer (e.g. on a pathological input) must
            // not kill the whole indexing run — record it as a non-fatal error
            // and continue with the next crate.
            let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
                collector.collect_crate(krate)
            }));
            if let Err(payload) = result {
                let error = OwnedStorageError {
                    id: collector.alloc_id(),
                    message: format!(
                        "indexer panicked while collecting crate {crate_root_path}: {}",
                        panic_payload_message(payload.as_ref())
                    ),
                    translation_unit: crate_root_path,
                    fatal: false,
                    indexed: true,
                };
                collector.storage.errors.push(error);
            }
        }

        // Second pass: reference edges, resolved semantically across all crates.
        let sema = Semantics::new(db);
        collector.collect_semantic_edges(&sema);

        collector.storage.next_id = collector.next_id;
        collector.storage
    })
}

/// In `impl [Trait for] Type { … }`, child Type nodes appear in source order:
///   - with `for` token: first child = trait type, second child = self type
///   - without `for` token: only child = self type
/// Returns the last path segment name of the self type, if path-shaped.
fn impl_self_type_segment_name(ast_impl: &ast::Impl) -> Option<String> {
    let type_nodes: Vec<ast::Type> = ast_impl
        .syntax()
        .children()
        .filter_map(ast::Type::cast)
        .collect();
    let has_for = ast_impl.for_token().is_some();
    let self_ty = if has_for && type_nodes.len() >= 2 {
        &type_nodes[1]
    } else if !has_for && !type_nodes.is_empty() {
        &type_nodes[0]
    } else {
        return None;
    };
    match self_ty {
        ast::Type::PathType(pt) => pt
            .path()
            .and_then(|p| p.segment())
            .and_then(|s| s.name_ref())
            .map(|n| n.text().to_string()),
        _ => None,
    }
}

/// Derive a module prefix from the file path relative to a source root.
/// E.g. `src/foo/bar.rs` → `foo::bar` (strips `src/` prefix and `.rs` suffix).
pub(super) fn module_prefix_from_path(file_path: &str, source_root: &str) -> String {
    let rel = std::path::Path::new(file_path)
        .strip_prefix(source_root)
        .unwrap_or(std::path::Path::new(file_path));

    let s = rel.to_string_lossy();
    let s = s.trim_end_matches(".rs");
    let s = s.trim_start_matches('/');

    // Convert path separators to `::` and drop `lib` / `main` / `mod` suffixes.
    let parts: Vec<&str> = s
        .split('/')
        .filter(|p| !p.is_empty() && *p != "lib" && *p != "main" && *p != "mod")
        .collect();
    parts.join("::")
}
