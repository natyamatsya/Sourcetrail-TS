use super::*;

// Helper: index a source string as if it were a file.
fn index_src(src: &str) -> OwnedIntermediateStorage {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("test.rs");
    std::fs::write(&path, src).unwrap();
    index_file(path.to_str().unwrap(), "")
}

/// Decode the NameHierarchy wire format back to a plain `::` qualified name for test assertions.
/// Wire format: `<delim>\tm<part1>\ts\tp[\tn<part2>\ts\tp...]`
fn decode_name(serialized: &str) -> String {
    let meta = "\tm";
    let name_sep = "\tn";
    let part_end = "\ts";
    if let Some(rest) = serialized.split_once(meta).map(|(_, r)| r) {
        let parts: Vec<&str> = rest.split(name_sep).collect();
        let decoded: Vec<&str> = parts
            .iter()
            .map(|p| p.split(part_end).next().unwrap_or(p))
            .collect();
        return decoded.join("::");
    }
    serialized.to_owned()
}

fn node_names(storage: &OwnedIntermediateStorage) -> Vec<String> {
    storage
        .nodes
        .iter()
        .map(|n| decode_name(&n.serialized_name))
        .collect()
}

fn has_node(storage: &OwnedIntermediateStorage, name: &str) -> bool {
    storage
        .nodes
        .iter()
        .any(|n| decode_name(&n.serialized_name) == name)
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
    assert!(has_node(&s, "hello_world"), "nodes: {:?}", node_names(&s));
    assert!(node_kinds(&s).contains(&NODE_FUNCTION));
}

#[test]
fn extracts_struct() {
    let s = index_src("pub struct MyStruct { x: i32 }");
    assert!(has_node(&s, "MyStruct"), "nodes: {:?}", node_names(&s));
    assert!(node_kinds(&s).contains(&NODE_STRUCT));
}

#[test]
fn extracts_enum() {
    let s = index_src("pub enum Color { Red, Green, Blue }");
    assert!(has_node(&s, "Color"), "nodes: {:?}", node_names(&s));
    assert!(node_kinds(&s).contains(&NODE_ENUM));
}

#[test]
fn extracts_union() {
    let s = index_src("pub union MyUnion { a: u32, b: f32 }");
    assert!(has_node(&s, "MyUnion"), "nodes: {:?}", node_names(&s));
    assert!(node_kinds(&s).contains(&NODE_UNION));
}

#[test]
fn extracts_trait() {
    let s = index_src("pub trait Drawable { fn draw(&self); }");
    assert!(has_node(&s, "Drawable"), "nodes: {:?}", node_names(&s));
    assert!(node_kinds(&s).contains(&NODE_INTERFACE));
}

#[test]
fn extracts_type_alias() {
    let s = index_src("pub type Meters = f64;");
    assert!(has_node(&s, "Meters"), "nodes: {:?}", node_names(&s));
    assert!(node_kinds(&s).contains(&NODE_TYPEDEF));
}

#[test]
fn extracts_mod() {
    let s = index_src("pub mod geometry { pub fn area() -> f64 { 0.0 } }");
    assert!(has_node(&s, "geometry"), "nodes: {:?}", node_names(&s));
    assert!(node_kinds(&s).contains(&NODE_MODULE));
}

#[test]
fn extracts_const() {
    let s = index_src("pub const MAX_SIZE: usize = 1024;");
    assert!(has_node(&s, "MAX_SIZE"), "nodes: {:?}", node_names(&s));
    assert!(node_kinds(&s).contains(&NODE_GLOBAL_VARIABLE));
}

#[test]
fn extracts_static() {
    let s = index_src("pub static COUNTER: u32 = 0;");
    assert!(has_node(&s, "COUNTER"), "nodes: {:?}", node_names(&s));
    assert!(node_kinds(&s).contains(&NODE_GLOBAL_VARIABLE));
}

#[test]
fn extracts_macro_def() {
    let s = index_src("macro_rules! my_macro { () => {}; }");
    assert!(has_node(&s, "my_macro"), "nodes: {:?}", node_names(&s));
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
    assert!(has_node(&s, "do_work"));
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

    assert!(has_node(&s, "a::Item"), "nodes: {:?}", node_names(&s));
    assert!(has_node(&s, "b::Item"), "nodes: {:?}", node_names(&s));
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
        .find(|n| decode_name(&n.serialized_name) == src)
        .map(|n| n.id);
    let tgt_id = s
        .nodes
        .iter()
        .find(|n| decode_name(&n.serialized_name) == tgt)
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

    assert!(has_node(&s, "Wrapper::T"), "nodes: {:?}", node_names(&s));
    assert!(has_edge(&s, EDGE_MEMBER, "Wrapper", "Wrapper::T"));
}

#[test]
fn const_generic_parameter_emits_member_node() {
    let s = index_src("pub struct Buffer<const N: usize> { pub bytes: [u8; N] }");

    assert!(has_node(&s, "Buffer::N"), "nodes: {:?}", node_names(&s));
    assert!(has_edge(&s, EDGE_MEMBER, "Buffer", "Buffer::N"));
}

#[test]
fn lifetime_bound_emits_type_parameter_node_and_usage_edge() {
    let s = index_src("pub struct Holder<'a, T: 'a> { pub t: T }");

    assert!(has_node(&s, "Holder::'a"), "nodes: {:?}", node_names(&s));
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

// -----------------------------------------------------------------------
// Struct fields, enum variants, impl methods
// -----------------------------------------------------------------------

const NODE_FIELD: i32 = 1 << 11;
const NODE_ENUM_CONSTANT: i32 = 1 << 15;
const NODE_METHOD: i32 = 1 << 13;

#[test]
fn struct_fields_are_collected() {
    let s = index_src("pub struct Point { pub x: f32, pub y: f32 }");
    assert!(has_node(&s, "Point::x"), "nodes: {:?}", node_names(&s));
    assert!(has_node(&s, "Point::y"), "nodes: {:?}", node_names(&s));
    assert!(has_edge(&s, EDGE_MEMBER, "Point", "Point::x"));
    assert!(has_edge(&s, EDGE_MEMBER, "Point", "Point::y"));
    let field_x = s
        .nodes
        .iter()
        .find(|n| decode_name(&n.serialized_name) == "Point::x")
        .unwrap();
    assert_eq!(field_x.type_, NODE_FIELD);
}

#[test]
fn enum_variants_are_collected() {
    let s = index_src("pub enum Color { Red, Green, Blue }");
    assert!(has_node(&s, "Color::Red"), "nodes: {:?}", node_names(&s));
    assert!(has_node(&s, "Color::Green"), "nodes: {:?}", node_names(&s));
    assert!(has_node(&s, "Color::Blue"), "nodes: {:?}", node_names(&s));
    assert!(has_edge(&s, EDGE_MEMBER, "Color", "Color::Red"));
    let variant = s
        .nodes
        .iter()
        .find(|n| decode_name(&n.serialized_name) == "Color::Red")
        .unwrap();
    assert_eq!(variant.type_, NODE_ENUM_CONSTANT);
}

#[test]
fn impl_methods_are_collected() {
    let s = index_src("pub struct Counter(u32); impl Counter { pub fn inc(&mut self) {} pub fn get(&self) -> u32 { self.0 } }");
    assert!(has_node(&s, "Counter::inc"), "nodes: {:?}", node_names(&s));
    assert!(has_node(&s, "Counter::get"), "nodes: {:?}", node_names(&s));
    assert!(has_edge(&s, EDGE_MEMBER, "Counter", "Counter::inc"));
    assert!(has_edge(&s, EDGE_MEMBER, "Counter", "Counter::get"));
    let method = s
        .nodes
        .iter()
        .find(|n| decode_name(&n.serialized_name) == "Counter::inc")
        .unwrap();
    assert_eq!(method.type_, NODE_METHOD);
}

// -----------------------------------------------------------------------
// Call edges
// -----------------------------------------------------------------------

const EDGE_CALL: i32 = 1 << 3;

#[test]
fn call_edge_emitted_between_functions() {
    let s = index_src("pub fn bar() {} pub fn foo() { bar(); }");
    assert!(has_node(&s, "foo"), "nodes: {:?}", node_names(&s));
    assert!(has_node(&s, "bar"), "nodes: {:?}", node_names(&s));
    assert!(
        has_edge(&s, EDGE_CALL, "foo", "bar"),
        "expected EDGE_CALL from foo to bar, edges: {:?}",
        s.edges
    );
}

#[test]
fn call_edge_emitted_for_method_call() {
    let s = index_src(
        "pub struct S; impl S { pub fn helper(&self) {} pub fn run(&self) { self.helper(); } }",
    );
    assert!(has_node(&s, "S::helper"), "nodes: {:?}", node_names(&s));
    assert!(has_node(&s, "S::run"), "nodes: {:?}", node_names(&s));
    assert!(
        has_edge(&s, EDGE_CALL, "S::run", "S::helper"),
        "expected EDGE_CALL from S::run to S::helper, edges: {:?}",
        s.edges
    );
}

// -----------------------------------------------------------------------
// Semantic resolution — cases string matching cannot disambiguate
// -----------------------------------------------------------------------

#[test]
fn method_call_resolves_to_the_right_type() {
    // Two types with the same method name: name-based matching is ambiguous
    // (and used to drop the edge); semantic resolution picks the right one.
    let s = index_src(
        "pub struct A; impl A { pub fn go(&self) {} }
         pub struct B; impl B { pub fn go(&self) {} }
         pub fn run(a: A) { a.go(); }",
    );
    assert!(
        has_edge(&s, EDGE_CALL, "run", "A::go"),
        "expected EDGE_CALL from run to A::go, edges: {:?}",
        s.edges
    );
    assert!(
        !has_edge(&s, EDGE_CALL, "run", "B::go"),
        "must not link run to B::go, edges: {:?}",
        s.edges
    );
}

#[test]
fn call_resolves_to_the_right_module() {
    // Same function name in two modules; the use-import decides.
    let s = index_src(
        "pub mod first { pub fn work() {} }
         pub mod second { pub fn work() {} }
         pub fn run() { second::work(); }",
    );
    assert!(
        has_edge(&s, EDGE_CALL, "run", "second::work"),
        "expected EDGE_CALL from run to second::work, edges: {:?}",
        s.edges
    );
    assert!(
        !has_edge(&s, EDGE_CALL, "run", "first::work"),
        "must not link run to first::work, edges: {:?}",
        s.edges
    );
}

#[test]
fn impl_trait_with_qualified_path_resolves_to_the_right_trait() {
    // Two traits with the same name in different modules.
    let s = index_src(
        "pub mod one { pub trait Marker {} }
         pub mod two { pub trait Marker {} }
         pub struct S;
         impl two::Marker for S {}",
    );
    assert!(
        has_edge(&s, EDGE_INHERITANCE, "S", "two::Marker"),
        "expected EDGE_INHERITANCE from S to two::Marker, edges: {:?}",
        s.edges
    );
    assert!(
        !has_edge(&s, EDGE_INHERITANCE, "S", "one::Marker"),
        "must not link S to one::Marker, edges: {:?}",
        s.edges
    );
}

#[test]
fn call_edges_are_deduplicated() {
    // Two calls to the same function from the same caller → one edge.
    let s = index_src("pub fn bar() {} pub fn foo() { bar(); bar(); }");
    let call_edges: Vec<_> = s.edges.iter().filter(|e| e.type_ == EDGE_CALL).collect();
    assert_eq!(
        call_edges.len(),
        1,
        "expected exactly one deduplicated EDGE_CALL, edges: {:?}",
        s.edges
    );
}
