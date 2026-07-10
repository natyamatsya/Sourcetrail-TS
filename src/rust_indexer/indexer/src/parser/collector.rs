use std::collections::{HashMap, HashSet};

use either::Either;
use ra_ap_hir::{
    Adt, AsAssocItem, AssocItem, Crate, FieldSource, HasSource, ModuleDef, PathResolution,
    Semantics,
};
use ra_ap_ide_db::RootDatabase;
use ra_ap_ide_db::base_db::SourceDatabase;
use ra_ap_ide_db::line_index::LineIndex;
use ra_ap_syntax::ast::{self, HasGenericParams, HasName, HasTypeBounds};
use ra_ap_syntax::{AstNode, SyntaxKind};
use ra_ap_vfs::Vfs;

use crate::ipc::storage::{
    OwnedIntermediateStorage, OwnedStorageEdge, OwnedStorageError, OwnedStorageFile,
    OwnedStorageNode, OwnedStorageOccurrence, OwnedStorageSourceLocation, OwnedStorageSymbol,
};

use super::{
    DEFINITION_EXPLICIT, EDGE_CALL, EDGE_INHERITANCE, EDGE_MEMBER, EDGE_TYPE_USAGE, EDGE_USAGE,
    LOCATION_SCOPE, LOCATION_TOKEN, NODE_ENUM, NODE_ENUM_CONSTANT, NODE_FIELD, NODE_FILE,
    NODE_FUNCTION, NODE_GLOBAL_VARIABLE, NODE_INTERFACE, NODE_MACRO, NODE_METHOD, NODE_MODULE,
    NODE_STRUCT, NODE_TYPE_PARAMETER, NODE_TYPEDEF, NODE_UNION,
};

#[derive(Debug, Clone)]
struct SourceContext {
    file_path: String,
    source_text: String,
}

#[derive(Debug, Clone)]
enum BoundTarget {
    Type(String),
    Lifetime(String),
}

/// Identity key for an emitted definition, so that semantically resolved
/// references (Semantics::resolve_path / resolve_method_call) can be mapped
/// back to the exact node — no string matching involved.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
enum DefKey {
    Def(ModuleDef),
    Field(ra_ap_hir::Field),
}

/// Per-file context for recording reference locations.
struct RefFile {
    file_node_id: i64,
    line_index: LineIndex,
}

/// Text range of a path's last segment name token — the clickable reference token.
fn path_name_range(path: &ast::Path) -> Option<ra_ap_syntax::TextRange> {
    Some(path.segment()?.name_ref()?.syntax().text_range())
}

struct Collector<'db> {
    db: &'db RootDatabase,
    vfs: &'db Vfs,
    /// file_path → StorageFile id
    file_ids: HashMap<String, i64>,
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
    #[expect(
        dead_code,
        reason = "convenience ctor for future callers without a progress callback"
    )]
    fn new(db: &'db RootDatabase, vfs: &'db Vfs) -> Self {
        Self::with_callback(db, vfs, |_| {})
    }

    fn with_callback(
        db: &'db RootDatabase,
        vfs: &'db Vfs,
        on_file: impl FnMut(&str) + 'db,
    ) -> Self {
        Self {
            db,
            vfs,
            file_ids: HashMap::new(),
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

    fn def_id(&self, key: DefKey) -> Option<i64> {
        self.def_ids.get(&key).copied()
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

    fn add_def(
        &mut self,
        name: &str,
        node_kind: i32,
        file_path: &str,
        range: ra_ap_syntax::TextRange,
        source_text: &str,
    ) {
        let node_id = self.alloc_id();
        let loc_id = self.alloc_id();
        let file_node_id = self.file_node_id(file_path);

        self.node_ids.insert(name.to_owned(), node_id);
        self.storage.nodes.push(OwnedStorageNode {
            id: node_id,
            type_: node_kind,
            serialized_name: serialize_name(name),
        });
        self.storage.symbols.push(OwnedStorageSymbol {
            id: node_id,
            definition_kind: DEFINITION_EXPLICIT,
        });

        let line_index = LineIndex::new(source_text);
        let start = line_index.line_col(range.start());
        let end = line_index.line_col(range.end());

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
        let source_id = self.resolve_node_id(source_name);
        let target_id = self.resolve_node_id(target_name);
        if let (Some(src), Some(tgt)) = (source_id, target_id) {
            self.push_edge(edge_type, src, tgt);
        }
    }

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

    fn source_context_from_in_file<N: AstNode>(
        &self,
        in_file: &ra_ap_hir::InFile<N>,
    ) -> Option<SourceContext> {
        let editioned_file_id = in_file.file_id.original_file(self.db);
        let vfs_file_id = editioned_file_id.file_id(self.db);
        let file_path = self.vfs_path(vfs_file_id)?;
        let source_text = self.db.file_text(vfs_file_id).text(self.db).to_string();
        Some(SourceContext {
            file_path,
            source_text,
        })
    }

    fn add_def_with_context(
        &mut self,
        name: &str,
        node_kind: i32,
        range: ra_ap_syntax::TextRange,
        ctx: &SourceContext,
    ) {
        self.add_def(name, node_kind, &ctx.file_path, range, &ctx.source_text);
    }

    fn ensure_type_parameter_node(
        &mut self,
        owner_name: &str,
        parameter_name: &str,
        range: ra_ap_syntax::TextRange,
        ctx: &SourceContext,
    ) -> String {
        let qualified_parameter_name = format!("{owner_name}::{parameter_name}");
        let exists = self.node_ids.contains_key(&qualified_parameter_name);
        if !exists {
            self.add_def_with_context(&qualified_parameter_name, NODE_TYPE_PARAMETER, range, ctx);
            self.add_edge(EDGE_MEMBER, owner_name, &qualified_parameter_name);
        }
        qualified_parameter_name
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
                self.collect_impl(&module_prefix, imp);
                self.collect_impl_items(&module_prefix, imp);
            }
        }
        // Call and inheritance edges are emitted afterwards by the semantic
        // reference pass (collect_semantic_edges), once all crates' nodes are
        // registered in def_ids.
    }

    /// Walk one `impl` block's generic bounds:
    ///   trait bounds on the impl's type params → EDGE_TYPE_USAGE from impl self-type to bound.
    /// (`impl Trait for Type` inheritance edges are emitted semantically in
    /// collect_semantic_edges.)
    fn collect_impl(&mut self, module_prefix: &str, imp: ra_ap_hir::Impl) {
        let Some(src) = imp.source(self.db) else {
            return;
        };
        let ast_impl = &src.value;
        let source_context = self.source_context_from_in_file(&src);

        let Some(self_ty_name) = impl_self_type_segment_name(ast_impl) else {
            return;
        };
        let self_ty_name = Self::qualify_in_module(module_prefix, &self_ty_name);

        // Trait bounds on the impl's own type parameters → EDGE_TYPE_USAGE
        self.emit_ast_generic_bounds(
            &self_ty_name,
            ast_impl.generic_param_list(),
            source_context.as_ref(),
        );
    }

    /// Emit EDGE_TYPE_USAGE edges for trait bounds on a generic item's type params.
    /// Works on the AST (via HasSource); bound targets resolve by name.
    fn collect_generic_bounds<N>(&mut self, item_name: &str, src: Option<ra_ap_hir::InFile<N>>)
    where
        N: ra_ap_syntax::AstNode + HasGenericParams,
    {
        if let Some(in_file) = src {
            let source_context = self.source_context_from_in_file(&in_file);
            self.emit_ast_generic_bounds(
                item_name,
                in_file.value.generic_param_list(),
                source_context.as_ref(),
            );
        }
    }

    /// Extract trait bounds from an AST `GenericParamList` and emit EDGE_TYPE_USAGE.
    /// Handles both inline bounds (`T: Trait`) and `where` clauses.
    fn emit_ast_generic_bounds(
        &mut self,
        item_name: &str,
        param_list: Option<ast::GenericParamList>,
        source_context: Option<&SourceContext>,
    ) {
        let Some(params) = param_list else { return };
        for param in params.type_or_const_params() {
            match param {
                ast::TypeOrConstParam::Type(tp) => {
                    if let Some(ctx) = source_context {
                        if let Some(name) = tp.name() {
                            self.ensure_type_parameter_node(
                                item_name,
                                &name.text().to_string(),
                                tp.syntax().text_range(),
                                ctx,
                            );
                        }
                    }

                    if let Some(bounds) = tp.type_bound_list() {
                        for bound in bounds.bounds() {
                            match bound_target(&bound) {
                                Some(BoundTarget::Type(bound_name)) => {
                                    self.add_edge(EDGE_TYPE_USAGE, item_name, &bound_name);
                                }
                                Some(BoundTarget::Lifetime(lifetime_name)) => {
                                    let Some(ctx) = source_context else { continue };
                                    let lifetime_node_name = self.ensure_type_parameter_node(
                                        item_name,
                                        &lifetime_name,
                                        bound.syntax().text_range(),
                                        ctx,
                                    );
                                    self.add_edge(EDGE_TYPE_USAGE, item_name, &lifetime_node_name);
                                }
                                None => {}
                            }
                        }
                    }
                }
                ast::TypeOrConstParam::Const(cp) => {
                    let const_param_name = if let Some(ctx) = source_context {
                        cp.name().map(|name| {
                            self.ensure_type_parameter_node(
                                item_name,
                                &name.text().to_string(),
                                cp.syntax().text_range(),
                                ctx,
                            )
                        })
                    } else {
                        None
                    };

                    let Some(type_name) = cp.ty().as_ref().and_then(type_target_name) else {
                        continue;
                    };

                    if let Some(parameter_name) = const_param_name {
                        self.add_edge(EDGE_TYPE_USAGE, &parameter_name, &type_name);
                        continue;
                    }
                    self.add_edge(EDGE_TYPE_USAGE, item_name, &type_name);
                }
            }
        }
        // Also handle `where T: Trait` clauses.
        if let Some(where_clause) = params
            .syntax()
            .parent()
            .and_then(|p| p.children().find_map(ast::WhereClause::cast))
        {
            for pred in where_clause.predicates() {
                if let Some(bounds) = pred.type_bound_list() {
                    for bound in bounds.bounds() {
                        match bound_target(&bound) {
                            Some(BoundTarget::Type(bound_name)) => {
                                self.add_edge(EDGE_TYPE_USAGE, item_name, &bound_name);
                            }
                            Some(BoundTarget::Lifetime(lifetime_name)) => {
                                let Some(ctx) = source_context else { continue };
                                let lifetime_node_name = self.ensure_type_parameter_node(
                                    item_name,
                                    &lifetime_name,
                                    bound.syntax().text_range(),
                                    ctx,
                                );
                                self.add_edge(EDGE_TYPE_USAGE, item_name, &lifetime_node_name);
                            }
                            None => {}
                        }
                    }
                }
            }
        }
    }

    fn collect_trait_details(
        &mut self,
        trait_name: &str,
        t: ra_ap_hir::Trait,
        src: Option<ra_ap_hir::InFile<ast::Trait>>,
    ) {
        // Supertrait bounds from the AST (`trait A: B`).
        if let Some(in_file) = src {
            if let Some(ctx) = self.source_context_from_in_file(&in_file) {
                let ast_trait = in_file.value;
                if let Some(bounds) = ast_trait.type_bound_list() {
                    for bound in bounds.bounds() {
                        match bound_target(&bound) {
                            Some(BoundTarget::Type(bound_name)) => {
                                self.add_edge(EDGE_INHERITANCE, trait_name, &bound_name);
                            }
                            Some(BoundTarget::Lifetime(lifetime_name)) => {
                                let lifetime_node_name = self.ensure_type_parameter_node(
                                    trait_name,
                                    &lifetime_name,
                                    bound.syntax().text_range(),
                                    &ctx,
                                );
                                self.add_edge(EDGE_TYPE_USAGE, trait_name, &lifetime_node_name);
                            }
                            None => {}
                        }
                    }
                }
            }
        }

        // Associated items via HIR, so each gets a DefKey registration.
        for item in t.items(self.db) {
            match item {
                AssocItem::Function(f) => {
                    let fname = f.name(self.db).as_str().to_string();
                    let qualified_name = format!("{trait_name}::{fname}");
                    self.emit_from_source(f.source(self.db), &qualified_name, NODE_METHOD);
                    self.register_def(DefKey::Def(ModuleDef::Function(f)), &qualified_name);
                    self.add_edge(EDGE_MEMBER, trait_name, &qualified_name);
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
            let eid = field_src.file_id.original_file(self.db);
            let vfs_fid = eid.file_id(self.db);
            let range = match &field_src.value {
                FieldSource::Named(rf) => rf
                    .name()
                    .map(|n| n.syntax().text_range())
                    .unwrap_or_else(|| rf.syntax().text_range()),
                FieldSource::Pos(tf) => tf.syntax().text_range(),
            };
            let Some(file_path) = self.vfs_path(vfs_fid) else {
                continue;
            };
            let source_text = self.db.file_text(vfs_fid).text(self.db).to_string();
            let file_node_id = self.file_node_id(&file_path);
            let node_id = self.alloc_id();
            let loc_id = self.alloc_id();
            self.node_ids.insert(qualified_name.clone(), node_id);
            self.storage.nodes.push(OwnedStorageNode {
                id: node_id,
                type_: NODE_FIELD,
                serialized_name: serialize_name(&qualified_name),
            });
            self.storage.symbols.push(OwnedStorageSymbol {
                id: node_id,
                definition_kind: DEFINITION_EXPLICIT,
            });
            let line_index = LineIndex::new(&source_text);
            let start = line_index.line_col(range.start());
            let end = line_index.line_col(range.end());
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
            self.def_ids.insert(DefKey::Field(field), node_id);
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
        let Some(src) = imp.source(self.db) else {
            return;
        };
        let ast_impl = &src.value;
        let Some(self_ty_name) = impl_self_type_segment_name(ast_impl) else {
            return;
        };
        let owner_name = Self::qualify_in_module(module_prefix, &self_ty_name);

        for item in imp.items(self.db) {
            match item {
                AssocItem::Function(f) => {
                    let fname = f.name(self.db).as_str().to_string();
                    let qualified_name = format!("{owner_name}::{fname}");
                    let exists = self.node_ids.contains_key(&qualified_name);
                    if !exists {
                        let fsrc = f.source(self.db);
                        self.emit_from_source(fsrc.clone(), &qualified_name, NODE_METHOD);
                        self.collect_generic_bounds(&qualified_name, fsrc);
                        self.add_edge(EDGE_MEMBER, &owner_name, &qualified_name);
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

    /// Walk every collected local file through `Semantics` and emit reference
    /// edges whose targets are resolved semantically (exact def identity), plus
    /// a TOKEN source location + occurrence per reference site, attached to the
    /// edge — mirroring the C++ side (ParserClientImpl::recordReference).
    /// Falls back to name-based lookup (`resolve_node_id`) per site when
    /// resolution fails, e.g. inside unexpanded macros.
    fn collect_semantic_edges(&mut self, sema: &Semantics<'db, RootDatabase>) {
        let files: Vec<ra_ap_vfs::FileId> = self.local_files.clone();
        for vfs_fid in files {
            let Some(file_path) = self.vfs_path(vfs_fid) else {
                continue;
            };
            let source_text = self.db.file_text(vfs_fid).text(self.db).to_string();
            let file = RefFile {
                file_node_id: self.file_node_id(&file_path),
                line_index: LineIndex::new(&source_text),
            };
            let source_file = sema.parse_guess_edition(vfs_fid);
            for node in source_file.syntax().descendants() {
                match node.kind() {
                    SyntaxKind::IMPL => {
                        if let Some(impl_ast) = ast::Impl::cast(node) {
                            self.emit_impl_inheritance(sema, &impl_ast, &file);
                        }
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
                        self.emit_reference(sema, EDGE_CALL, call.syntax(), target, range, &file);
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
                        self.emit_reference(sema, EDGE_CALL, call.syntax(), target, range, &file);
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
                        self.emit_reference(sema, EDGE_USAGE, pe.syntax(), target, range, &file);
                    }
                    SyntaxKind::PATH_TYPE => {
                        let Some(pt) = ast::PathType::cast(node) else {
                            continue;
                        };
                        self.emit_type_reference(sema, &pt, &file);
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
                        self.emit_reference(sema, EDGE_USAGE, fe.syntax(), target, range, &file);
                    }
                    SyntaxKind::RECORD_EXPR_FIELD => {
                        let Some(ref_field) = ast::RecordExprField::cast(node) else {
                            continue;
                        };
                        let target = sema
                            .resolve_record_field(&ref_field)
                            .and_then(|(field, _, _)| self.def_id(DefKey::Field(field)));
                        let range = ref_field.name_ref().map(|n| n.syntax().text_range());
                        self.emit_reference(
                            sema,
                            EDGE_USAGE,
                            ref_field.syntax(),
                            target,
                            range,
                            &file,
                        );
                    }
                    _ => {}
                }
            }
        }
    }

    /// Type reference (`PathType`) → EDGE_TYPE_USAGE from the enclosing item.
    /// Types in an `impl` header are skipped: the inheritance/bounds handling
    /// covers them, and the header itself is not an emitted node.
    fn emit_type_reference(
        &mut self,
        sema: &Semantics<'db, RootDatabase>,
        pt: &ast::PathType,
        file: &RefFile,
    ) {
        let Some(path) = pt.path() else { return };
        let target = match sema.resolve_path(&path) {
            Some(PathResolution::Def(def)) => self.def_id(DefKey::Def(def)),
            _ => None,
        };
        let range = path_name_range(&path);
        self.emit_reference_edge(
            sema,
            EDGE_TYPE_USAGE,
            pt.syntax(),
            target,
            range,
            file,
            /*allow_impl_context=*/ false,
        );
    }

    /// Emit a reference edge + occurrence from the enclosing context of
    /// `ref_site` to `target`, recording a TOKEN location at `range`.
    fn emit_reference(
        &mut self,
        sema: &Semantics<'db, RootDatabase>,
        edge_type: i32,
        ref_site: &ra_ap_syntax::SyntaxNode,
        target: Option<i64>,
        range: Option<ra_ap_syntax::TextRange>,
        file: &RefFile,
    ) {
        self.emit_reference_edge(sema, edge_type, ref_site, target, range, file, true);
    }

    fn emit_reference_edge(
        &mut self,
        sema: &Semantics<'db, RootDatabase>,
        edge_type: i32,
        ref_site: &ra_ap_syntax::SyntaxNode,
        target: Option<i64>,
        range: Option<ra_ap_syntax::TextRange>,
        file: &RefFile,
        allow_impl_context: bool,
    ) {
        let Some(target) = target else { return };
        let Some(context) = self.enclosing_context_id(sema, ref_site, allow_impl_context) else {
            return;
        };
        if context == target {
            return;
        }
        let edge_id = self.push_edge(edge_type, context, target);
        if let Some(range) = range {
            self.add_reference_location(edge_id, range, file);
        }
    }

    /// Record a TOKEN source location + occurrence for `element_id` (an edge).
    fn add_reference_location(
        &mut self,
        element_id: i64,
        range: ra_ap_syntax::TextRange,
        file: &RefFile,
    ) {
        let loc_id = self.alloc_id();
        let start = file.line_index.line_col(range.start());
        let end = file.line_index.line_col(range.end());
        self.storage
            .source_locations
            .push(OwnedStorageSourceLocation {
                id: loc_id,
                file_node_id: file.file_node_id,
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

    /// `impl Trait for Type` → EDGE_INHERITANCE from Type to Trait.
    /// Trait and self type are resolved via `Semantics::resolve_path`; each side
    /// independently falls back to name lookup on the path segment text.
    fn emit_impl_inheritance(
        &mut self,
        sema: &Semantics<'db, RootDatabase>,
        impl_ast: &ast::Impl,
        file: &RefFile,
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
            let edge_id = self.push_edge(EDGE_INHERITANCE, self_id, trait_id);
            if let ast::Type::PathType(pt) = &type_nodes[0] {
                if let Some(range) = pt.path().as_ref().and_then(path_name_range) {
                    self.add_reference_location(edge_id, range, file);
                }
            }
        }
    }

    /// Resolve a path-shaped AST type to an emitted node id: semantically first,
    /// then by segment-name fallback.
    fn resolve_type_node(
        &self,
        sema: &Semantics<'db, RootDatabase>,
        ty: &ast::Type,
    ) -> Option<i64> {
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
        sema: &Semantics<'db, RootDatabase>,
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
                self.emit_from_source(src.clone(), &qualified_name, kind);
                self.register_def(DefKey::Def(def), &qualified_name);
                self.collect_generic_bounds(&qualified_name, src);
            }
            ModuleDef::Adt(adt) => match adt {
                Adt::Struct(s) => {
                    let name = s.name(self.db).as_str().to_string();
                    let qualified_name = Self::qualify_in_module(module_prefix, &name);
                    let src = s.source(self.db);
                    self.emit_from_source(src.clone(), &qualified_name, NODE_STRUCT);
                    self.register_def(DefKey::Def(def), &qualified_name);
                    self.collect_generic_bounds(&qualified_name, src);
                    self.collect_struct_fields(&qualified_name, s);
                }
                Adt::Enum(e) => {
                    let name = e.name(self.db).as_str().to_string();
                    let qualified_name = Self::qualify_in_module(module_prefix, &name);
                    let src = e.source(self.db);
                    self.emit_from_source(src.clone(), &qualified_name, NODE_ENUM);
                    self.register_def(DefKey::Def(def), &qualified_name);
                    self.collect_generic_bounds(&qualified_name, src);
                    self.collect_enum_variants(&qualified_name, e);
                }
                Adt::Union(u) => {
                    let name = u.name(self.db).as_str().to_string();
                    let qualified_name = Self::qualify_in_module(module_prefix, &name);
                    let src = u.source(self.db);
                    self.emit_from_source(src.clone(), &qualified_name, NODE_UNION);
                    self.register_def(DefKey::Def(def), &qualified_name);
                    self.collect_generic_bounds(&qualified_name, src);
                    self.collect_union_fields(&qualified_name, u);
                }
            },
            ModuleDef::Trait(t) => {
                let name = t.name(self.db).as_str().to_string();
                let qualified_name = Self::qualify_in_module(module_prefix, &name);
                let src = t.source(self.db);
                self.emit_from_source(src.clone(), &qualified_name, NODE_INTERFACE);
                self.register_def(DefKey::Def(def), &qualified_name);
                self.collect_generic_bounds(&qualified_name, src.clone());
                self.collect_trait_details(&qualified_name, t, src);
            }
            ModuleDef::TypeAlias(ta) => {
                let name = ta.name(self.db).as_str().to_string();
                let qualified_name = Self::qualify_in_module(module_prefix, &name);
                let src = ta.source(self.db);
                self.emit_from_source(src.clone(), &qualified_name, NODE_TYPEDEF);
                self.register_def(DefKey::Def(def), &qualified_name);
                self.collect_generic_bounds(&qualified_name, src);
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
            let editioned_file_id = range_in_file.file_id.original_file(self.db);
            let vfs_file_id = editioned_file_id.file_id(self.db);
            let Some(file_path) = self.vfs_path(vfs_file_id) else {
                return;
            };
            let source_text = self.db.file_text(vfs_file_id).text(self.db).to_string();
            self.add_def(
                name,
                NODE_MODULE,
                &file_path,
                range_in_file.value,
                &source_text,
            );
        }
    }

    /// Generic helper: given an `InFile<ast::*>` source, extract the name
    /// token range and emit a node, plus a SCOPE location spanning the whole
    /// item (drives snippet extents in the code view).
    fn emit_from_source<N: AstNode + HasName>(
        &mut self,
        src: Option<ra_ap_hir::InFile<N>>,
        name: &str,
        kind: i32,
    ) {
        let Some(in_file) = src else { return };
        let Some(ctx) = self.source_context_from_in_file(&in_file) else {
            return;
        };
        let full_range = in_file.value.syntax().text_range();
        let range = in_file
            .value
            .name()
            .map(|n| n.syntax().text_range())
            .unwrap_or(full_range);
        self.add_def_with_context(name, kind, range, &ctx);
        if full_range != range {
            self.add_scope_location(name, full_range, &ctx);
        }
    }

    /// Record a SCOPE location for the already-emitted node `name`.
    fn add_scope_location(
        &mut self,
        name: &str,
        range: ra_ap_syntax::TextRange,
        ctx: &SourceContext,
    ) {
        let Some(&node_id) = self.node_ids.get(name) else {
            return;
        };
        let file_node_id = self.file_node_id(&ctx.file_path);
        let loc_id = self.alloc_id();
        let line_index = LineIndex::new(&ctx.source_text);
        let start = line_index.line_col(range.start());
        let end = line_index.line_col(range.end());
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
        let Some(ctx) = self.source_context_from_in_file(&in_file) else {
            return;
        };
        let range = in_file.value.syntax().text_range();
        self.add_def_with_context(name, kind, range, &ctx);
    }
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
            // Skip library crates (std, core, deps) — only index local crates.
            let vfs_fid = krate.root_file(db);
            let is_local = vfs
                .file_path(vfs_fid)
                .as_path()
                .map(|p| {
                    let s = p.to_string();
                    !s.contains("/.cargo/") && !s.contains("/rustup/")
                })
                .unwrap_or(false);
            if !is_local {
                continue;
            }
            collector.collect_crate(krate);
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

fn bound_target(bound: &ast::TypeBound) -> Option<BoundTarget> {
    if let Some(ty) = bound.ty() {
        if let ast::Type::PathType(pt) = ty {
            let segment = pt.path()?.segment()?;
            return Some(BoundTarget::Type(segment.name_ref()?.text().to_string()));
        }
        return None;
    }

    let text = bound.syntax().text().to_string();
    let lifetime = extract_lifetime_name(&text)?;
    Some(BoundTarget::Lifetime(lifetime))
}

fn extract_lifetime_name(text: &str) -> Option<String> {
    let trimmed = text.trim();
    let mut chars = trimmed.chars();
    if chars.next()? != '\'' {
        return None;
    }

    let mut out = String::from("'");
    for c in chars {
        if c.is_ascii_alphanumeric() || c == '_' {
            out.push(c);
        } else {
            break;
        }
    }

    if out.len() > 1 {
        return Some(out);
    }
    None
}

fn type_target_name(ty: &ast::Type) -> Option<String> {
    if let ast::Type::PathType(pt) = ty {
        let segment = pt.path()?.segment()?;
        return Some(segment.name_ref()?.text().to_string());
    }
    None
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
