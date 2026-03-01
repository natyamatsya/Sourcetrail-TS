// Rust source parser — Phase 3b using ra_ap_hir (rust-analyzer HIR).
//
// Loads a full Cargo crate via ra_ap_load_cargo::load_workspace_at() and
// walks every ModuleDef in every local module to emit an OwnedIntermediateStorage.
//
// What is emitted per definition:
//   - StorageFile  for every source file that contains at least one definition
//   - StorageNode  for each named item (fn, struct, enum, union, trait, type
//                  alias, const, static, macro, module)
//   - StorageSymbol (DefinitionKind = EXPLICIT = 2) for each node
//   - StorageSourceLocation pointing at the name token of the item
//   - StorageOccurrence linking node → location
//   - StorageEdge  for impl/trait relationships:
//       EDGE_INHERITANCE  (1<<4 = 16)  — `impl Trait for Type`
//       EDGE_TYPE_USAGE   (1<<1 = 2)   — trait bounds on type params
//   - StorageError  for load / analysis failures
//
// Node kind constants mirror NodeKind.h (bitmask values):
//   NODE_MODULE          = 1 << 3  = 8
//   NODE_STRUCT          = 1 << 6  = 64
//   NODE_INTERFACE       = 1 << 8  = 256   (trait)
//   NODE_GLOBAL_VARIABLE = 1 << 10 = 1024  (const / static)
//   NODE_FUNCTION        = 1 << 12 = 4096
//   NODE_METHOD          = 1 << 13 = 8192
//   NODE_ENUM            = 1 << 14 = 16384
//   NODE_TYPEDEF         = 1 << 16 = 65536
//   NODE_TYPE_PARAMETER  = 1 << 17 = 131072
//   NODE_MACRO           = 1 << 19 = 524288
//   NODE_UNION           = 1 << 20 = 1048576
//
// Edge type constants mirror Edge::EdgeType in Edge.h (bitmask values):
//   EDGE_MEMBER       = 1 << 0 = 1
//   EDGE_TYPE_USAGE   = 1 << 1 = 2
//   EDGE_INHERITANCE  = 1 << 4 = 16
//
// DefinitionKind: NONE = 0, IMPLICIT = 1, EXPLICIT = 2
// LocationType:   TOKEN = 0, SCOPE = 1, QUALIFIER = 2

use std::collections::{HashMap, HashSet};
use std::path::Path;

use ra_ap_hir::{Adt, AsAssocItem, Crate, HasSource, ModuleDef};
use ra_ap_ide_db::base_db::{RootQueryDb, SourceDatabase};
use ra_ap_ide_db::line_index::LineIndex;
use ra_ap_ide_db::RootDatabase;
use ra_ap_load_cargo::{load_workspace_at, LoadCargoConfig, ProcMacroServerChoice};
use ra_ap_project_model::CargoConfig;
use ra_ap_syntax::ast::{self, HasGenericParams, HasName, HasTypeBounds};
use ra_ap_syntax::AstNode;
use ra_ap_vfs::Vfs;

use crate::ipc::storage::{
    OwnedIntermediateStorage, OwnedStorageEdge, OwnedStorageError, OwnedStorageFile,
    OwnedStorageNode, OwnedStorageOccurrence, OwnedStorageSourceLocation, OwnedStorageSymbol,
};

const NODE_MODULE: i32 = 1 << 3;
const NODE_STRUCT: i32 = 1 << 6;
const NODE_INTERFACE: i32 = 1 << 8;
const NODE_GLOBAL_VARIABLE: i32 = 1 << 10;
const NODE_FUNCTION: i32 = 1 << 12;
const NODE_METHOD: i32 = 1 << 13;
const NODE_ENUM: i32 = 1 << 14;
const NODE_TYPEDEF: i32 = 1 << 16;
const NODE_TYPE_PARAMETER: i32 = 1 << 17;
const NODE_MACRO: i32 = 1 << 19;
const NODE_UNION: i32 = 1 << 20;

const DEFINITION_EXPLICIT: i32 = 2;
const LOCATION_TOKEN: i32 = 0;

const EDGE_MEMBER: i32 = 1 << 0;
const EDGE_TYPE_USAGE: i32 = 1 << 1;
const EDGE_INHERITANCE: i32 = 1 << 4;

// ---------------------------------------------------------------------------
// Public entry point: index a whole Cargo crate
// ---------------------------------------------------------------------------

/// Index the Cargo crate whose `Cargo.toml` lives in `crate_root`.
/// Returns a populated `OwnedIntermediateStorage` covering all local source files.
pub fn index_crate(crate_root: &Path) -> OwnedIntermediateStorage {
    let cargo_config = CargoConfig::default();
    let load_config = LoadCargoConfig {
        load_out_dirs_from_check: false,
        with_proc_macro_server: ProcMacroServerChoice::None,
        prefill_caches: false,
        proc_macro_processes: 1,
    };

    let (db, vfs, _proc_macro) =
        match load_workspace_at(crate_root, &cargo_config, &load_config, &|_| {}) {
            Ok(r) => r,
            Err(e) => {
                let mut storage = OwnedIntermediateStorage::default();
                storage.errors.push(OwnedStorageError {
                    id: 1,
                    message: format!("Failed to load crate: {e}"),
                    translation_unit: crate_root.display().to_string(),
                    fatal: true,
                    indexed: false,
                });
                return storage;
            }
        };

    collect_from_db(&db, &vfs)
}

// ---------------------------------------------------------------------------
// Compat shim: index a single file by wrapping it in a temp crate.
// Used by the existing per-file tests and by main.rs (one command = one file).
// ---------------------------------------------------------------------------

/// Index a single `.rs` file.
/// Creates a minimal temporary Cargo crate, writes the file as `src/lib.rs`,
/// loads it via `ra_ap_hir`, and returns the storage.
pub fn index_file(file_path: &str, _module_prefix: &str) -> OwnedIntermediateStorage {
    let source = match std::fs::read_to_string(file_path) {
        Ok(s) => s,
        Err(e) => return error_storage(file_path, &format!("Failed to read file: {e}")),
    };

    // Build a temp crate: Cargo.toml + src/lib.rs
    let tmp = match tempfile::tempdir() {
        Ok(d) => d,
        Err(e) => return error_storage(file_path, &format!("tempdir: {e}")),
    };
    let src_dir = tmp.path().join("src");
    if let Err(e) = std::fs::create_dir_all(&src_dir) {
        return error_storage(file_path, &format!("mkdir src: {e}"));
    }
    let cargo_toml =
        format!("[package]\nname = \"_idx\"\nversion = \"0.0.0\"\nedition = \"2021\"\n");
    if let Err(e) = std::fs::write(tmp.path().join("Cargo.toml"), &cargo_toml) {
        return error_storage(file_path, &format!("write Cargo.toml: {e}"));
    }
    if let Err(e) = std::fs::write(src_dir.join("lib.rs"), &source) {
        return error_storage(file_path, &format!("write lib.rs: {e}"));
    }

    let mut storage = index_crate(tmp.path());

    // Rewrite the synthetic file path back to the original path so callers
    // see the real file path in the storage.
    for f in &mut storage.files {
        f.file_path = file_path.to_owned();
    }
    storage
}

fn error_storage(file_path: &str, msg: &str) -> OwnedIntermediateStorage {
    let mut storage = OwnedIntermediateStorage::default();
    storage.next_id = 2;
    storage.files.push(OwnedStorageFile {
        id: 1,
        file_path: file_path.to_owned(),
        language_identifier: "rust".to_owned(),
        indexed: false,
        complete: false,
    });
    storage.errors.push(OwnedStorageError {
        id: 2,
        message: msg.to_owned(),
        translation_unit: file_path.to_owned(),
        fatal: true,
        indexed: false,
    });
    storage
}

// ---------------------------------------------------------------------------
// Core collection logic
// ---------------------------------------------------------------------------

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
    next_id: i64,
    storage: OwnedIntermediateStorage,
}

impl<'db> Collector<'db> {
    fn new(db: &'db RootDatabase, vfs: &'db Vfs) -> Self {
        Self {
            db,
            vfs,
            file_ids: HashMap::new(),
            next_id: 1,
            storage: OwnedIntermediateStorage::default(),
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

        self.storage.nodes.push(OwnedStorageNode {
            id: node_id,
            type_: node_kind,
            serialized_name: name.to_owned(),
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
        if let Some(id) = self
            .storage
            .nodes
            .iter()
            .find(|n| n.serialized_name == query)
            .map(|n| n.id)
        {
            return Some(id);
        }

        let suffix = format!("::{query}");
        let mut matches = self
            .storage
            .nodes
            .iter()
            .filter(|n| n.serialized_name.ends_with(&suffix))
            .map(|n| n.id);

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
        let exists = self
            .storage
            .nodes
            .iter()
            .any(|n| n.serialized_name == qualified_parameter_name);
        if !exists {
            self.add_def_with_context(&qualified_parameter_name, NODE_TYPE_PARAMETER, range, ctx);
            self.add_edge(EDGE_MEMBER, owner_name, &qualified_parameter_name);
        }
        qualified_parameter_name
    }

    fn collect_crate(&mut self, krate: Crate) {
        for module in krate.modules(self.db) {
            let module_prefix = self.module_prefix(module);
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
            // Walk impl blocks for EDGE_INHERITANCE and EDGE_TYPE_USAGE.
            for imp in module.impl_defs(self.db) {
                self.collect_impl(&module_prefix, imp);
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
            if let ast::TypeOrConstParam::Type(tp) = param {
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
                }
                Adt::Enum(e) => {
                    let name = e.name(self.db).as_str().to_string();
                    let qualified_name = Self::qualify_in_module(module_prefix, &name);
                    let src = e.source(self.db);
                    self.emit_from_source(src.clone(), &qualified_name, NODE_ENUM);
                    self.collect_generic_bounds(&qualified_name, src);
                }
                Adt::Union(u) => {
                    let name = u.name(self.db).as_str().to_string();
                    let qualified_name = Self::qualify_in_module(module_prefix, &name);
                    let src = u.source(self.db);
                    self.emit_from_source(src.clone(), &qualified_name, NODE_UNION);
                    self.collect_generic_bounds(&qualified_name, src);
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
    fn emit_from_source<N: AstNode>(
        &mut self,
        src: Option<ra_ap_hir::InFile<N>>,
        name: &str,
        kind: i32,
    ) {
        let Some(in_file) = src else { return };
        let Some(ctx) = self.source_context_from_in_file(&in_file) else {
            return;
        };
        let syntax = in_file.value.syntax().text_range();
        self.add_def_with_context(name, kind, syntax, &ctx);
    }
}

fn collect_from_db(db: &RootDatabase, vfs: &Vfs) -> OwnedIntermediateStorage {
    let mut collector = Collector::new(db, vfs);

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

#[cfg(test)]
mod tests {
    use super::*;

    // Helper: index a source string as if it were a file.
    fn index_src(src: &str) -> OwnedIntermediateStorage {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("test.rs");
        std::fs::write(&path, src).unwrap();
        index_file(path.to_str().unwrap(), "")
    }

    fn node_names(storage: &OwnedIntermediateStorage) -> Vec<&str> {
        storage
            .nodes
            .iter()
            .map(|n| n.serialized_name.as_str())
            .collect()
    }

    fn node_kinds(storage: &OwnedIntermediateStorage) -> Vec<i32> {
        storage.nodes.iter().map(|n| n.type_).collect()
    }

    // -----------------------------------------------------------------------
    // One test per symbol kind
    // -----------------------------------------------------------------------

    #[test]
    fn extracts_function() {
        let s = index_src("pub fn hello_world() {}");
        assert!(
            node_names(&s).contains(&"hello_world"),
            "nodes: {:?}",
            node_names(&s)
        );
        assert!(node_kinds(&s).contains(&NODE_FUNCTION));
    }

    #[test]
    fn extracts_struct() {
        let s = index_src("pub struct MyStruct { x: i32 }");
        assert!(
            node_names(&s).contains(&"MyStruct"),
            "nodes: {:?}",
            node_names(&s)
        );
        assert!(node_kinds(&s).contains(&NODE_STRUCT));
    }

    #[test]
    fn extracts_enum() {
        let s = index_src("pub enum Color { Red, Green, Blue }");
        assert!(
            node_names(&s).contains(&"Color"),
            "nodes: {:?}",
            node_names(&s)
        );
        assert!(node_kinds(&s).contains(&NODE_ENUM));
    }

    #[test]
    fn extracts_union() {
        let s = index_src("pub union MyUnion { a: u32, b: f32 }");
        assert!(
            node_names(&s).contains(&"MyUnion"),
            "nodes: {:?}",
            node_names(&s)
        );
        assert!(node_kinds(&s).contains(&NODE_UNION));
    }

    #[test]
    fn extracts_trait() {
        let s = index_src("pub trait Drawable { fn draw(&self); }");
        assert!(
            node_names(&s).contains(&"Drawable"),
            "nodes: {:?}",
            node_names(&s)
        );
        assert!(node_kinds(&s).contains(&NODE_INTERFACE));
    }

    #[test]
    fn extracts_type_alias() {
        let s = index_src("pub type Meters = f64;");
        assert!(
            node_names(&s).contains(&"Meters"),
            "nodes: {:?}",
            node_names(&s)
        );
        assert!(node_kinds(&s).contains(&NODE_TYPEDEF));
    }

    #[test]
    fn extracts_mod() {
        let s = index_src("pub mod geometry { pub fn area() -> f64 { 0.0 } }");
        assert!(
            node_names(&s).contains(&"geometry"),
            "nodes: {:?}",
            node_names(&s)
        );
        assert!(node_kinds(&s).contains(&NODE_MODULE));
    }

    #[test]
    fn extracts_const() {
        let s = index_src("pub const MAX_SIZE: usize = 1024;");
        assert!(
            node_names(&s).contains(&"MAX_SIZE"),
            "nodes: {:?}",
            node_names(&s)
        );
        assert!(node_kinds(&s).contains(&NODE_GLOBAL_VARIABLE));
    }

    #[test]
    fn extracts_static() {
        let s = index_src("pub static COUNTER: u32 = 0;");
        assert!(
            node_names(&s).contains(&"COUNTER"),
            "nodes: {:?}",
            node_names(&s)
        );
        assert!(node_kinds(&s).contains(&NODE_GLOBAL_VARIABLE));
    }

    #[test]
    fn extracts_macro_def() {
        let s = index_src("macro_rules! my_macro { () => {}; }");
        assert!(
            node_names(&s).contains(&"my_macro"),
            "nodes: {:?}",
            node_names(&s)
        );
        assert!(node_kinds(&s).contains(&NODE_MACRO));
    }

    // -----------------------------------------------------------------------
    // Module prefix qualification
    // -----------------------------------------------------------------------

    #[test]
    fn qualifies_name_with_module_prefix() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("test.rs");
        std::fs::write(&path, "pub fn do_work() {}").unwrap();
        let s = index_file(path.to_str().unwrap(), "");
        // Without a source root that strips the path, prefix is empty.
        assert!(node_names(&s).contains(&"do_work"));
    }

    #[test]
    fn module_prefix_from_path_strips_src_prefix() {
        assert_eq!(
            module_prefix_from_path("/proj/src/foo/bar.rs", "/proj/src"),
            "foo::bar"
        );
    }

    #[test]
    fn module_prefix_from_path_drops_lib_suffix() {
        assert_eq!(module_prefix_from_path("/proj/src/lib.rs", "/proj/src"), "");
    }

    #[test]
    fn module_prefix_from_path_drops_main_suffix() {
        assert_eq!(
            module_prefix_from_path("/proj/src/main.rs", "/proj/src"),
            ""
        );
    }

    #[test]
    fn disambiguates_same_symbol_name_across_modules() {
        let s = index_src("pub mod a { pub struct Item; } pub mod b { pub struct Item; }");

        assert!(
            node_names(&s).contains(&"a::Item"),
            "nodes: {:?}",
            node_names(&s)
        );
        assert!(
            node_names(&s).contains(&"b::Item"),
            "nodes: {:?}",
            node_names(&s)
        );
    }

    // -----------------------------------------------------------------------
    // Error handling
    // -----------------------------------------------------------------------

    #[test]
    fn reports_error_for_missing_file() {
        let s = index_file("/nonexistent/path/file.rs", "");
        assert!(!s.errors.is_empty(), "expected an error for missing file");
        assert!(s.errors[0].fatal);
    }

    #[test]
    fn reports_error_for_invalid_syntax() {
        let s = index_src("fn broken( { }");
        // ra_ap_hir uses error recovery, so parse errors are non-fatal.
        assert!(!s.errors.is_empty(), "expected a parse error");
    }

    // -----------------------------------------------------------------------
    // Storage invariants
    // -----------------------------------------------------------------------

    #[test]
    fn every_node_has_a_symbol_and_occurrence() {
        let s = index_src("pub fn f() {} pub struct S {} pub enum E {} pub trait T {}");
        assert_eq!(s.nodes.len(), s.symbols.len(), "node/symbol count mismatch");
        assert_eq!(
            s.nodes.len(),
            s.occurrences.len(),
            "node/occurrence count mismatch"
        );
    }

    #[test]
    fn every_node_has_a_source_location() {
        let s = index_src("pub fn alpha() {} pub fn beta() {}");
        assert_eq!(s.nodes.len(), s.source_locations.len());
    }

    #[test]
    fn file_entry_is_marked_indexed() {
        let s = index_src("pub fn x() {}");
        assert_eq!(s.files.len(), 1);
        assert!(s.files[0].indexed);
        assert!(s.files[0].complete);
        assert_eq!(s.files[0].language_identifier, "rust");
    }

    #[test]
    fn multiple_items_all_extracted() {
        let s = index_src(
            "pub fn a() {} pub struct B {} pub enum C {} pub trait D {} pub type E = u8;",
        );
        assert_eq!(
            s.nodes.len(),
            5,
            "expected 5 nodes, got: {:?}",
            node_names(&s)
        );
    }

    // -----------------------------------------------------------------------
    // Edge tests — EDGE_INHERITANCE and EDGE_TYPE_USAGE
    // -----------------------------------------------------------------------

    fn edge_types(s: &OwnedIntermediateStorage) -> Vec<i32> {
        s.edges.iter().map(|e| e.type_).collect()
    }

    fn has_edge(s: &OwnedIntermediateStorage, edge_type: i32, src: &str, tgt: &str) -> bool {
        let src_id = s
            .nodes
            .iter()
            .find(|n| n.serialized_name == src)
            .map(|n| n.id);
        let tgt_id = s
            .nodes
            .iter()
            .find(|n| n.serialized_name == tgt)
            .map(|n| n.id);
        match (src_id, tgt_id) {
            (Some(sid), Some(tid)) => s.edges.iter().any(|e| {
                e.type_ == edge_type && e.source_node_id == sid && e.target_node_id == tid
            }),
            _ => false,
        }
    }

    #[test]
    fn impl_trait_emits_inheritance_edge() {
        let s = index_src("pub trait Greet {} pub struct Person; impl Greet for Person {}");
        assert!(
            edge_types(&s).contains(&EDGE_INHERITANCE),
            "expected EDGE_INHERITANCE, edges: {:?}",
            s.edges
        );
        assert!(has_edge(&s, EDGE_INHERITANCE, "Person", "Greet"));
    }

    #[test]
    fn trait_supertrait_emits_inheritance_edge() {
        let s = index_src("pub trait Base {} pub trait Child: Base {}");
        assert!(has_edge(&s, EDGE_INHERITANCE, "Child", "Base"));
    }

    #[test]
    fn trait_associated_items_emit_member_edges() {
        let s = index_src("pub trait Api { type Item; const ID: usize; fn run(&self); }");

        assert!(has_edge(&s, EDGE_MEMBER, "Api", "Api::Item"));
        assert!(has_edge(&s, EDGE_MEMBER, "Api", "Api::ID"));
        assert!(has_edge(&s, EDGE_MEMBER, "Api", "Api::run"));
    }

    #[test]
    fn trait_bound_on_struct_emits_type_usage_edge() {
        let s = index_src("pub trait Summary {} pub struct Wrapper<T: Summary> { pub val: T }");
        assert!(
            edge_types(&s).contains(&EDGE_TYPE_USAGE),
            "expected EDGE_TYPE_USAGE for trait bound, edges: {:?}",
            s.edges
        );
    }

    #[test]
    fn trait_bound_on_fn_emits_type_usage_edge() {
        let s = index_src("pub trait Display {} pub fn print<T: Display>(val: T) {}");
        assert!(
            edge_types(&s).contains(&EDGE_TYPE_USAGE),
            "expected EDGE_TYPE_USAGE for fn trait bound, edges: {:?}",
            s.edges
        );
        assert!(has_edge(&s, EDGE_TYPE_USAGE, "print", "Display"));
    }

    #[test]
    fn lifetime_bound_emits_type_parameter_node_and_usage_edge() {
        let s = index_src("pub struct Holder<'a, T: 'a> { pub t: T }");

        assert!(
            node_names(&s).contains(&"Holder::'a"),
            "nodes: {:?}",
            node_names(&s)
        );
        assert!(has_edge(&s, EDGE_MEMBER, "Holder", "Holder::'a"));
        assert!(has_edge(&s, EDGE_TYPE_USAGE, "Holder", "Holder::'a"));
    }

    #[test]
    fn plain_impl_block_emits_no_inheritance_edge() {
        let s = index_src("pub struct Counter; impl Counter { pub fn inc(&self) {} }");
        assert!(
            !edge_types(&s).contains(&EDGE_INHERITANCE),
            "plain impl should not emit EDGE_INHERITANCE"
        );
    }
}

/// Extract a bound target from an AST `TypeBound`.
/// - type bounds (e.g. `Display`, `std::fmt::Display`) map to `BoundTarget::Type`
/// - lifetime bounds (e.g. `'a`) map to `BoundTarget::Lifetime`
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

/// Derive a module prefix from the file path relative to a source root.
/// E.g. `src/foo/bar.rs` → `foo::bar`  (strips `src/` prefix and `.rs` suffix).
pub fn module_prefix_from_path(file_path: &str, source_root: &str) -> String {
    let rel = Path::new(file_path)
        .strip_prefix(source_root)
        .unwrap_or(Path::new(file_path));

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
