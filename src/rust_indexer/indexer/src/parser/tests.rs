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

const NODE_FILE: i32 = 1 << 18;

fn non_file_nodes(s: &OwnedIntermediateStorage) -> usize {
    s.nodes.iter().filter(|n| n.type_ != NODE_FILE).count()
}

#[test]
fn every_node_has_a_symbol_and_occurrence() {
    let s = index_src("pub fn f() {} pub struct S {} pub enum E {} pub trait T {}");
    let nf = non_file_nodes(&s);
    assert_eq!(nf, s.symbols.len(), "node/symbol count mismatch");
    assert_eq!(nf, s.occurrences.len(), "node/occurrence count mismatch");
}

#[test]
fn every_node_has_a_source_location() {
    let s = index_src("pub fn alpha() {} pub fn beta() {}");
    assert_eq!(non_file_nodes(&s), s.source_locations.len());
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
    let s =
        index_src("pub fn a() {} pub struct B {} pub enum C {} pub trait D {} pub type E = u8;");
    assert_eq!(
        non_file_nodes(&s),
        5,
        "expected 5 non-file nodes, got: {:?}",
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
        (Some(sid), Some(tid)) => s
            .edges
            .iter()
            .any(|e| e.type_ == edge_type && e.source_node_id == sid && e.target_node_id == tid),
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
fn type_generic_parameter_emits_member_node() {
    let s = index_src("pub struct Wrapper<T> { pub value: T }");

    assert!(
        node_names(&s).contains(&"Wrapper::T"),
        "nodes: {:?}",
        node_names(&s)
    );
    assert!(has_edge(&s, EDGE_MEMBER, "Wrapper", "Wrapper::T"));
}

#[test]
fn const_generic_parameter_emits_member_node() {
    let s = index_src("pub struct Buffer<const N: usize> { pub bytes: [u8; N] }");

    assert!(
        node_names(&s).contains(&"Buffer::N"),
        "nodes: {:?}",
        node_names(&s)
    );
    assert!(has_edge(&s, EDGE_MEMBER, "Buffer", "Buffer::N"));
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
