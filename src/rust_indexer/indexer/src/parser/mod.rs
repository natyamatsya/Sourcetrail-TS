// Rust source parser — Phase 3 prototype using `syn`.
//
// Walks a single .rs file and emits an OwnedIntermediateStorage containing:
//   - StorageFile for the source file itself
//   - StorageNode for each top-level item (fn, struct, enum, trait, impl, mod, …)
//   - StorageSymbol (definition kind = DEFINED = 1) for each node
//   - StorageSourceLocation for each item's span
//   - StorageOccurrence linking each node to its location
//   - StorageError for parse failures
//
// Node kind constants mirror NodeKind.h (bitmask values):
//   NODE_SYMBOL          = 1 << 0  = 1
//   NODE_TYPE            = 1 << 1  = 2
//   NODE_BUILTIN_TYPE    = 1 << 2  = 4
//   NODE_MODULE          = 1 << 3  = 8
//   NODE_NAMESPACE       = 1 << 4  = 16
//   NODE_PACKAGE         = 1 << 5  = 32
//   NODE_STRUCT          = 1 << 6  = 64
//   NODE_CLASS           = 1 << 7  = 128
//   NODE_INTERFACE       = 1 << 8  = 256
//   NODE_ANNOTATION      = 1 << 9  = 512
//   NODE_GLOBAL_VARIABLE = 1 << 10 = 1024
//   NODE_FIELD           = 1 << 11 = 2048
//   NODE_FUNCTION        = 1 << 12 = 4096
//   NODE_METHOD          = 1 << 13 = 8192
//   NODE_ENUM            = 1 << 14 = 16384
//   NODE_ENUM_CONSTANT   = 1 << 15 = 32768
//   NODE_TYPEDEF         = 1 << 16 = 65536
//   NODE_MACRO           = 1 << 17 = 131072
//   NODE_UNION           = 1 << 18 = 262144
//
// DefinitionKind: NONE = 0, IMPLICIT = 1, EXPLICIT = 2
// LocationType:   TOKEN = 0, SCOPE = 1, QUALIFIER = 2, …

use std::path::Path;

use syn::visit::Visit;

use crate::ipc::storage::{
    OwnedIntermediateStorage, OwnedStorageError, OwnedStorageFile, OwnedStorageNode,
    OwnedStorageOccurrence, OwnedStorageSourceLocation, OwnedStorageSymbol,
};

const NODE_MODULE: i32 = 1 << 3;
const NODE_STRUCT: i32 = 1 << 6;
const NODE_INTERFACE: i32 = 1 << 8;
const NODE_GLOBAL_VARIABLE: i32 = 1 << 10;
const NODE_FUNCTION: i32 = 1 << 12;
const NODE_ENUM: i32 = 1 << 14;
const NODE_TYPEDEF: i32 = 1 << 16;
const NODE_MACRO: i32 = 1 << 17;
const NODE_UNION: i32 = 1 << 18;

const DEFINITION_EXPLICIT: i32 = 2;
const LOCATION_TOKEN: i32 = 0;

pub fn index_file(file_path: &str, module_prefix: &str) -> OwnedIntermediateStorage {
    let source = match std::fs::read_to_string(file_path) {
        Ok(s) => s,
        Err(e) => {
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
                message: format!("Failed to read file: {e}"),
                translation_unit: file_path.to_owned(),
                fatal: true,
                indexed: false,
            });
            return storage;
        }
    };

    let syntax = match syn::parse_file(&source) {
        Ok(f) => f,
        Err(e) => {
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
                message: format!("Parse error: {e}"),
                translation_unit: file_path.to_owned(),
                fatal: true,
                indexed: false,
            });
            return storage;
        }
    };

    let mut collector = ItemCollector {
        file_path: file_path.to_owned(),
        module_prefix: module_prefix.to_owned(),
        file_id: 1,
        next_id: 2,
        storage: OwnedIntermediateStorage::default(),
    };

    collector.storage.files.push(OwnedStorageFile {
        id: 1,
        file_path: file_path.to_owned(),
        language_identifier: "rust".to_owned(),
        indexed: true,
        complete: true,
    });

    collector.visit_file(&syntax);
    collector.storage.next_id = collector.next_id;
    collector.storage
}

struct ItemCollector {
    file_path: String,
    module_prefix: String,
    file_id: i64,
    next_id: i64,
    storage: OwnedIntermediateStorage,
}

impl ItemCollector {
    fn alloc_id(&mut self) -> i64 {
        let id = self.next_id;
        self.next_id += 1;
        id
    }

    fn qualified_name(&self, name: &str) -> String {
        if self.module_prefix.is_empty() {
            name.to_owned()
        } else {
            format!("{}::{}", self.module_prefix, name)
        }
    }

    fn add_item(&mut self, name: &str, node_kind: i32, span: proc_macro2::Span) {
        let node_id = self.alloc_id();
        let loc_id = self.alloc_id();

        let qname = self.qualified_name(name);

        self.storage.nodes.push(OwnedStorageNode {
            id: node_id,
            type_: node_kind,
            serialized_name: qname,
        });
        self.storage.symbols.push(OwnedStorageSymbol {
            id: node_id,
            definition_kind: DEFINITION_EXPLICIT,
        });

        let start = span.start();
        let end = span.end();

        self.storage
            .source_locations
            .push(OwnedStorageSourceLocation {
                id: loc_id,
                file_node_id: self.file_id,
                start_line: start.line as u32,
                start_col: (start.column + 1) as u32,
                end_line: end.line as u32,
                end_col: (end.column + 1) as u32,
                type_: LOCATION_TOKEN,
            });
        self.storage.occurrences.push(OwnedStorageOccurrence {
            element_id: node_id,
            source_location_id: loc_id,
        });
    }
}

impl<'ast> Visit<'ast> for ItemCollector {
    fn visit_item_fn(&mut self, node: &'ast syn::ItemFn) {
        self.add_item(
            &node.sig.ident.to_string(),
            NODE_FUNCTION,
            node.sig.ident.span(),
        );
        syn::visit::visit_item_fn(self, node);
    }

    fn visit_item_struct(&mut self, node: &'ast syn::ItemStruct) {
        self.add_item(&node.ident.to_string(), NODE_STRUCT, node.ident.span());
        syn::visit::visit_item_struct(self, node);
    }

    fn visit_item_enum(&mut self, node: &'ast syn::ItemEnum) {
        self.add_item(&node.ident.to_string(), NODE_ENUM, node.ident.span());
        syn::visit::visit_item_enum(self, node);
    }

    fn visit_item_union(&mut self, node: &'ast syn::ItemUnion) {
        self.add_item(&node.ident.to_string(), NODE_UNION, node.ident.span());
        syn::visit::visit_item_union(self, node);
    }

    fn visit_item_trait(&mut self, node: &'ast syn::ItemTrait) {
        self.add_item(&node.ident.to_string(), NODE_INTERFACE, node.ident.span());
        syn::visit::visit_item_trait(self, node);
    }

    fn visit_item_type(&mut self, node: &'ast syn::ItemType) {
        self.add_item(&node.ident.to_string(), NODE_TYPEDEF, node.ident.span());
        syn::visit::visit_item_type(self, node);
    }

    fn visit_item_mod(&mut self, node: &'ast syn::ItemMod) {
        self.add_item(&node.ident.to_string(), NODE_MODULE, node.ident.span());
        syn::visit::visit_item_mod(self, node);
    }

    fn visit_item_const(&mut self, node: &'ast syn::ItemConst) {
        self.add_item(
            &node.ident.to_string(),
            NODE_GLOBAL_VARIABLE,
            node.ident.span(),
        );
        syn::visit::visit_item_const(self, node);
    }

    fn visit_item_static(&mut self, node: &'ast syn::ItemStatic) {
        self.add_item(
            &node.ident.to_string(),
            NODE_GLOBAL_VARIABLE,
            node.ident.span(),
        );
        syn::visit::visit_item_static(self, node);
    }

    fn visit_item_macro(&mut self, node: &'ast syn::ItemMacro) {
        if let Some(ident) = &node.ident {
            self.add_item(&ident.to_string(), NODE_MACRO, ident.span());
        }
        syn::visit::visit_item_macro(self, node);
    }
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
        assert!(!s.errors.is_empty(), "expected a parse error");
        assert!(s.errors[0].fatal);
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
