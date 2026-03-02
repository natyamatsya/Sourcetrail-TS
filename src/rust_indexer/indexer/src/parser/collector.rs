use std::collections::{HashMap, HashSet};

use ra_ap_hir::{Adt, AsAssocItem, AssocItem, Crate, FieldSource, HasSource, ModuleDef};
use ra_ap_ide_db::base_db::{RootQueryDb, SourceDatabase};
use ra_ap_ide_db::line_index::LineIndex;
use ra_ap_ide_db::RootDatabase;
use ra_ap_syntax::ast::{self, HasGenericParams, HasName, HasTypeBounds};
use ra_ap_syntax::{AstNode, SyntaxKind};
use ra_ap_vfs::Vfs;

use crate::ipc::storage::{
    OwnedIntermediateStorage, OwnedStorageEdge, OwnedStorageError, OwnedStorageFile,
    OwnedStorageNode, OwnedStorageOccurrence, OwnedStorageSourceLocation, OwnedStorageSymbol,
};

use super::{
    DEFINITION_EXPLICIT, EDGE_CALL, EDGE_INHERITANCE, EDGE_MEMBER, EDGE_TYPE_USAGE, EDGE_USAGE,
    LOCATION_TOKEN, NODE_ENUM, NODE_ENUM_CONSTANT, NODE_FIELD, NODE_FILE, NODE_FUNCTION,
    NODE_GLOBAL_VARIABLE, NODE_INTERFACE, NODE_MACRO, NODE_METHOD, NODE_MODULE, NODE_STRUCT,
    NODE_TYPEDEF, NODE_TYPE_PARAMETER, NODE_UNION,
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

struct Collector<'db> {
    db: &'db RootDatabase,
    vfs: &'db Vfs,
    /// file_path → StorageFile id
    file_ids: HashMap<String, i64>,
    /// plain qualified name → node id (for edge resolution)
    node_ids: HashMap<String, i64>,
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
            next_id: 1,
            storage: OwnedIntermediateStorage::default(),
            on_file: Box::new(on_file),
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

    /// Emit a directed edge between two named nodes (looked up by serialized name).
    /// If either node hasn't been emitted yet the edge is silently dropped — this
    /// can happen for items from external crates that we intentionally skip.
    fn add_edge(&mut self, edge_type: i32, source_name: &str, target_name: &str) {
        let source_id = self.resolve_node_id(source_name);
        let target_id = self.resolve_node_id(target_name);
        if let (Some(src), Some(tgt)) = (source_id, target_id) {
            let edge_id = self.alloc_id();
            self.storage.edges.push(OwnedStorageEdge {
                id: edge_id,
                type_: edge_type,
                source_node_id: src,
                target_node_id: tgt,
            });
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
        let vfs_file_id = ra_ap_vfs::FileId::from_raw(editioned_file_id.file_id(self.db).index());
        let file_path = self.vfs_path(vfs_file_id)?;
        let raw_fid = editioned_file_id.file_id(self.db);
        let source_text = self.db.file_text(raw_fid).text(self.db).to_string();
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
                let vfs_fid = ra_ap_vfs::FileId::from_raw(editioned.file_id(self.db).index());
                if let Some(file_path) =
                    self.vfs.file_path(vfs_fid).as_path().map(|p| p.to_string())
                {
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
        // Second pass: emit EDGE_CALL edges by walking function bodies.
        // Must be done after all nodes are collected so node_ids is fully populated.
        let fns: Vec<(ra_ap_hir::Function, String)> = krate
            .modules(self.db)
            .into_iter()
            .flat_map(|m| m.declarations(self.db))
            .filter_map(|def| {
                if let ModuleDef::Function(f) = def {
                    let name = f.name(self.db).as_str().to_string();
                    Some((f, name))
                } else {
                    None
                }
            })
            .collect();
        for (f, caller_name) in &fns {
            let caller_qualified = self
                .node_ids
                .keys()
                .find(|k| k.as_str() == caller_name || k.ends_with(&format!("::{caller_name}")))
                .cloned();
            if let Some(caller_qualified) = caller_qualified {
                self.collect_fn_call_edges(*f, &caller_qualified);
            }
        }
        // Also collect call edges for impl methods.
        for module in krate.modules(self.db) {
            for imp in module.impl_defs(self.db) {
                let module_prefix = self.module_prefix(module);
                let Some(src) = imp.source(self.db) else {
                    continue;
                };
                let type_nodes: Vec<ast::Type> = src
                    .value
                    .syntax()
                    .children()
                    .filter_map(ast::Type::cast)
                    .collect();
                let has_for = src.value.for_token().is_some();
                let self_ty = if has_for && type_nodes.len() >= 2 {
                    Some(&type_nodes[1])
                } else if !has_for && !type_nodes.is_empty() {
                    Some(&type_nodes[0])
                } else {
                    continue;
                };
                let owner_name = match self_ty {
                    Some(ast::Type::PathType(pt)) => pt
                        .path()
                        .and_then(|p| p.segment())
                        .and_then(|s| s.name_ref())
                        .map(|n| Self::qualify_in_module(&module_prefix, &n.text().to_string())),
                    _ => None,
                };
                let Some(owner_name) = owner_name else {
                    continue;
                };
                for item in imp.items(self.db) {
                    if let AssocItem::Function(f) = item {
                        let fname = f.name(self.db).as_str().to_string();
                        let qualified = format!("{owner_name}::{fname}");
                        if self.node_ids.contains_key(&qualified) {
                            self.collect_fn_call_edges(f, &qualified);
                        }
                    }
                }
            }
        }
    }

    /// Walk one `impl` block using the AST only — no HIR type inference.
    ///   - `impl Trait for Type`  → EDGE_INHERITANCE from Type to Trait
    ///   - trait bounds on the impl's type params → EDGE_TYPE_USAGE from impl self-type to bound
    fn collect_impl(&mut self, module_prefix: &str, imp: ra_ap_hir::Impl) {
        // Use the AST source to avoid any HIR type-inference calls (Type::as_adt,
        // Type::display, etc.) which require the salsa next-solver TLS attachment.
        let Some(src) = imp.source(self.db) else {
            return;
        };
        let ast_impl = &src.value;
        let source_context = self.source_context_from_in_file(&src);

        // In `impl [Trait for] Type { … }`, child Type nodes appear in source order:
        //   - with `for` token: first child = trait type, second child = self type
        //   - without `for` token: only child = self type
        let type_nodes: Vec<ast::Type> = ast_impl
            .syntax()
            .children()
            .filter_map(ast::Type::cast)
            .collect();

        let has_for = ast_impl.for_token().is_some();
        let (trait_ty, self_ty) = if has_for && type_nodes.len() >= 2 {
            (Some(&type_nodes[0]), Some(&type_nodes[1]))
        } else if !has_for && type_nodes.len() >= 1 {
            (None, Some(&type_nodes[0]))
        } else {
            return;
        };

        let self_ty_name = match self_ty {
            Some(ast::Type::PathType(pt)) => pt
                .path()
                .and_then(|p| p.segment())
                .and_then(|s| s.name_ref())
                .map(|n| n.text().to_string()),
            _ => None,
        };
        let Some(self_ty_name) = self_ty_name else {
            return;
        };
        let self_ty_name = Self::qualify_in_module(module_prefix, &self_ty_name);

        // `impl Trait for Type` → EDGE_INHERITANCE
        if let Some(ast::Type::PathType(pt)) = trait_ty {
            if let Some(trait_name) = pt
                .path()
                .and_then(|p| p.segment())
                .and_then(|s| s.name_ref())
                .map(|n| n.text().to_string())
            {
                let trait_name = Self::qualify_in_module(module_prefix, &trait_name);
                self.add_edge(EDGE_INHERITANCE, &self_ty_name, &trait_name);
            }
        }

        // Trait bounds on the impl's own type parameters → EDGE_TYPE_USAGE
        self.emit_ast_generic_bounds(
            &self_ty_name,
            ast_impl.generic_param_list(),
            source_context.as_ref(),
        );
    }

    /// Emit EDGE_TYPE_USAGE edges for trait bounds on a generic item's type params.
    /// Uses the AST (via HasSource) to avoid the salsa next-solver TLS requirement
    /// of TypeParam::trait_bounds(db).
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
        src: Option<ra_ap_hir::InFile<ast::Trait>>,
    ) {
        let Some(in_file) = src else {
            return;
        };
        let Some(ctx) = self.source_context_from_in_file(&in_file) else {
            return;
        };

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

        if let Some(assoc_items) = ast_trait.assoc_item_list() {
            for assoc in assoc_items.assoc_items() {
                match assoc {
                    ast::AssocItem::Fn(fn_item) => {
                        let Some(name) = fn_item.name() else { continue };
                        let qualified_name = format!("{trait_name}::{}", name.text().to_string());
                        self.add_def_with_context(
                            &qualified_name,
                            NODE_METHOD,
                            fn_item.syntax().text_range(),
                            &ctx,
                        );
                        self.add_edge(EDGE_MEMBER, trait_name, &qualified_name);
                    }
                    ast::AssocItem::TypeAlias(type_alias) => {
                        let Some(name) = type_alias.name() else {
                            continue;
                        };
                        let qualified_name = format!("{trait_name}::{}", name.text().to_string());
                        self.add_def_with_context(
                            &qualified_name,
                            NODE_TYPEDEF,
                            type_alias.syntax().text_range(),
                            &ctx,
                        );
                        self.add_edge(EDGE_MEMBER, trait_name, &qualified_name);
                    }
                    ast::AssocItem::Const(const_item) => {
                        let Some(name) = const_item.name() else {
                            continue;
                        };
                        let qualified_name = format!("{trait_name}::{}", name.text().to_string());
                        self.add_def_with_context(
                            &qualified_name,
                            NODE_GLOBAL_VARIABLE,
                            const_item.syntax().text_range(),
                            &ctx,
                        );
                        self.add_edge(EDGE_MEMBER, trait_name, &qualified_name);
                    }
                    _ => {}
                }
            }
        }
    }

    fn collect_parse_errors(&mut self, module: ra_ap_hir::Module) {
        let Some(file_id) = module.as_source_file_id(self.db) else {
            return;
        };
        let errors = self.db.parse_errors(file_id);
        let Some(errors) = errors else { return };
        if errors.is_empty() {
            return;
        }
        let vfs_fid = ra_ap_vfs::FileId::from_raw(file_id.file_id(self.db).index());
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
            let raw_fid = eid.file_id(self.db).index();
            let range = match &field_src.value {
                FieldSource::Named(rf) => rf
                    .name()
                    .map(|n| n.syntax().text_range())
                    .unwrap_or_else(|| rf.syntax().text_range()),
                FieldSource::Pos(tf) => tf.syntax().text_range(),
            };
            let (file_id_raw, range) = (raw_fid, range);
            let vfs_fid = ra_ap_vfs::FileId::from_raw(file_id_raw);
            let Some(file_path) = self.vfs_path(vfs_fid) else {
                continue;
            };
            let editioned = field_src.file_id.original_file(self.db);
            let source_text = self
                .db
                .file_text(editioned.file_id(self.db))
                .text(self.db)
                .to_string();
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
            self.add_edge(EDGE_MEMBER, enum_name, &qualified_variant);
            self.collect_hir_fields(&qualified_variant, variant.fields(self.db));
        }
    }

    fn collect_impl_items(&mut self, module_prefix: &str, imp: ra_ap_hir::Impl) {
        let Some(src) = imp.source(self.db) else {
            return;
        };
        let ast_impl = &src.value;
        let type_nodes: Vec<ast::Type> = ast_impl
            .syntax()
            .children()
            .filter_map(ast::Type::cast)
            .collect();
        let has_for = ast_impl.for_token().is_some();
        let self_ty = if has_for && type_nodes.len() >= 2 {
            Some(&type_nodes[1])
        } else if !has_for && !type_nodes.is_empty() {
            Some(&type_nodes[0])
        } else {
            return;
        };
        let self_ty_name = match self_ty {
            Some(ast::Type::PathType(pt)) => pt
                .path()
                .and_then(|p| p.segment())
                .and_then(|s| s.name_ref())
                .map(|n| n.text().to_string()),
            _ => None,
        };
        let Some(self_ty_name) = self_ty_name else {
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
                }
            }
        }
    }

    /// Walk a function's AST body and emit EDGE_CALL for every call site whose
    /// callee resolves to a known node in `node_ids`.
    /// Uses AST only — no HIR type inference.
    fn collect_fn_call_edges(&mut self, f: ra_ap_hir::Function, caller_name: &str) {
        let Some(src) = f.source(self.db) else { return };
        let fn_node = src.value.syntax();

        // Collect all callee name strings from call expressions in the body.
        let mut callees: Vec<String> = Vec::new();

        for node in fn_node.descendants() {
            match node.kind() {
                SyntaxKind::CALL_EXPR => {
                    if let Some(call) = ast::CallExpr::cast(node) {
                        if let Some(expr) = call.expr() {
                            if let ast::Expr::PathExpr(pe) = expr {
                                if let Some(name) = pe
                                    .path()
                                    .and_then(|p| p.segment())
                                    .and_then(|s| s.name_ref())
                                {
                                    callees.push(name.text().to_string());
                                }
                            }
                        }
                    }
                }
                SyntaxKind::METHOD_CALL_EXPR => {
                    if let Some(call) = ast::MethodCallExpr::cast(node) {
                        if let Some(name_ref) = call.name_ref() {
                            callees.push(name_ref.text().to_string());
                        }
                    }
                }
                _ => {}
            }
        }

        let caller_name = caller_name.to_owned();
        for callee in callees {
            let callee_id = self.resolve_node_id(&callee);
            if let Some(callee_id) = callee_id {
                let caller_id = self.node_ids.get(&caller_name).copied();
                if let Some(caller_id) = caller_id {
                    if caller_id != callee_id {
                        let edge_id = self.alloc_id();
                        self.storage.edges.push(OwnedStorageEdge {
                            id: edge_id,
                            type_: EDGE_CALL,
                            source_node_id: caller_id,
                            target_node_id: callee_id,
                        });
                    }
                }
            }
        }
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
                self.collect_generic_bounds(&qualified_name, src);
            }
            ModuleDef::Adt(adt) => match adt {
                Adt::Struct(s) => {
                    let name = s.name(self.db).as_str().to_string();
                    let qualified_name = Self::qualify_in_module(module_prefix, &name);
                    let src = s.source(self.db);
                    self.emit_from_source(src.clone(), &qualified_name, NODE_STRUCT);
                    self.collect_generic_bounds(&qualified_name, src);
                    self.collect_struct_fields(&qualified_name, s);
                }
                Adt::Enum(e) => {
                    let name = e.name(self.db).as_str().to_string();
                    let qualified_name = Self::qualify_in_module(module_prefix, &name);
                    let src = e.source(self.db);
                    self.emit_from_source(src.clone(), &qualified_name, NODE_ENUM);
                    self.collect_generic_bounds(&qualified_name, src);
                    self.collect_enum_variants(&qualified_name, e);
                }
                Adt::Union(u) => {
                    let name = u.name(self.db).as_str().to_string();
                    let qualified_name = Self::qualify_in_module(module_prefix, &name);
                    let src = u.source(self.db);
                    self.emit_from_source(src.clone(), &qualified_name, NODE_UNION);
                    self.collect_generic_bounds(&qualified_name, src);
                    self.collect_union_fields(&qualified_name, u);
                }
            },
            ModuleDef::Trait(t) => {
                let name = t.name(self.db).as_str().to_string();
                let qualified_name = Self::qualify_in_module(module_prefix, &name);
                let src = t.source(self.db);
                self.emit_from_source(src.clone(), &qualified_name, NODE_INTERFACE);
                self.collect_generic_bounds(&qualified_name, src.clone());
                self.collect_trait_details(&qualified_name, src);
            }
            ModuleDef::TypeAlias(ta) => {
                let name = ta.name(self.db).as_str().to_string();
                let qualified_name = Self::qualify_in_module(module_prefix, &name);
                let src = ta.source(self.db);
                self.emit_from_source(src.clone(), &qualified_name, NODE_TYPEDEF);
                self.collect_generic_bounds(&qualified_name, src);
            }
            ModuleDef::Const(c) => {
                if let Some(name) = c.name(self.db) {
                    let name = name.as_str().to_string();
                    let qualified_name = Self::qualify_in_module(module_prefix, &name);
                    self.emit_from_source(c.source(self.db), &qualified_name, NODE_GLOBAL_VARIABLE);
                }
            }
            ModuleDef::Static(s) => {
                let name = s.name(self.db).as_str().to_string();
                let qualified_name = Self::qualify_in_module(module_prefix, &name);
                self.emit_from_source(s.source(self.db), &qualified_name, NODE_GLOBAL_VARIABLE);
            }
            ModuleDef::Macro(m) => {
                let name = m.name(self.db).as_str().to_string();
                let qualified_name = Self::qualify_in_module(module_prefix, &name);
                self.emit_from_source(m.source(self.db), &qualified_name, NODE_MACRO);
            }
            ModuleDef::Module(m) => {
                if let Some(name) = m.name(self.db) {
                    let name = name.as_str().to_string();
                    let qualified_name = Self::qualify_in_module(module_prefix, &name);
                    // declaration_source gives the `mod foo;` node (inline modules
                    // have no declaration node — they are covered by their parent).
                    self.emit_module_decl(m, &qualified_name);
                }
            }
            ModuleDef::BuiltinType(_) | ModuleDef::Variant(_) => {}
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
            let vfs_file_id =
                ra_ap_vfs::FileId::from_raw(editioned_file_id.file_id(self.db).index());
            let Some(file_path) = self.vfs_path(vfs_file_id) else {
                return;
            };
            let raw_fid = editioned_file_id.file_id(self.db);
            let source_text = self.db.file_text(raw_fid).text(self.db).to_string();
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
    /// token range and emit a node.
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
        let range = in_file
            .value
            .name()
            .map(|n| n.syntax().text_range())
            .unwrap_or_else(|| in_file.value.syntax().text_range());
        self.add_def_with_context(name, kind, range, &ctx);
    }

    /// Emit a node from a raw `InFile<N>` without requiring `HasName` — uses
    /// the whole syntax node range. Used for items that don't implement HasName
    /// (e.g. macro_rules! via legacy_macros, module decls).
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
    let mut collector = Collector::with_callback(db, vfs, on_file);

    for krate in Crate::all(db) {
        // Skip library crates (std, core, deps) — only index local crates.
        let root_fid = krate.root_file(db);
        let vfs_fid = ra_ap_vfs::FileId::from_raw(root_fid.index());
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

    collector.storage.next_id = collector.next_id;
    collector.storage
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
