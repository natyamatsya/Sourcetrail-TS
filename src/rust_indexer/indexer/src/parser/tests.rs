use super::*;

// Helper: index a source string as if it were a file.
fn index_src(src: &str) -> OwnedIntermediateStorage {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("test.rs");
    std::fs::write(&path, src).unwrap();
    index_file(path.to_str().unwrap(), "")
}

// Helper: like index_src, but with the sysroot loaded so builtin derives
// (`Clone`, `Debug`, …) resolve and expand. Noticeably slower — use only in
// tests that need macro expansion.
fn index_src_with_sysroot(src: &str) -> OwnedIntermediateStorage {
    let tmp = tempfile::tempdir().unwrap();
    scaffold_temp_crate(tmp.path(), src).unwrap();
    index_crate_with(
        tmp.path(),
        LoadProfile::SYSROOT,
        CargoOptions::default(),
        |_| {},
    )
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
    // Every node has at least its name-token occurrence (plus scope locations).
    for n in s.nodes.iter().filter(|n| n.type_ != NODE_FILE) {
        assert!(
            s.occurrences.iter().any(|o| o.element_id == n.id),
            "node without occurrence: {}",
            decode_name(&n.serialized_name)
        );
    }
}

#[test]
fn every_node_has_a_token_source_location() {
    let s = index_src("pub fn alpha() {} pub fn beta() {}");
    let token_locations = s.source_locations.iter().filter(|l| l.type_ == 0).count();
    assert_eq!(non_file_nodes(&s), token_locations);
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
    // The bound edge originates at the parameter node, not the item.
    assert!(
        has_edge(&s, EDGE_TYPE_USAGE, "print::T", "Display"),
        "expected EDGE_TYPE_USAGE print::T -> Display, edges: {:?}",
        s.edges
    );
    assert!(!has_edge(&s, EDGE_TYPE_USAGE, "print", "Display"));
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
    // `T: 'a` — the edge runs between the parameter nodes.
    assert!(
        has_edge(&s, EDGE_TYPE_USAGE, "Holder::T", "Holder::'a"),
        "expected EDGE_TYPE_USAGE Holder::T -> Holder::'a, edges: {:?}",
        s.edges
    );
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
    let s = index_src(
        "pub struct Counter(u32); impl Counter { pub fn inc(&mut self) {} pub fn get(&self) -> u32 { self.0 } }",
    );
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

// -----------------------------------------------------------------------
// Reference occurrences — locations recorded at usage sites, on the edge
// -----------------------------------------------------------------------

const EDGE_USAGE_T: i32 = 1 << 2;
const EDGE_TYPE_USAGE_T: i32 = 1 << 1;

fn edge_id_between(
    s: &OwnedIntermediateStorage,
    edge_type: i32,
    src: &str,
    tgt: &str,
) -> Option<i64> {
    let node_id = |name: &str| {
        s.nodes
            .iter()
            .find(|n| decode_name(&n.serialized_name) == name)
            .map(|n| n.id)
    };
    let (sid, tid) = (node_id(src)?, node_id(tgt)?);
    s.edges
        .iter()
        .find(|e| e.type_ == edge_type && e.source_node_id == sid && e.target_node_id == tid)
        .map(|e| e.id)
}

fn edge_occurrence_positions(s: &OwnedIntermediateStorage, edge_id: i64) -> Vec<(u32, u32)> {
    s.occurrences
        .iter()
        .filter(|o| o.element_id == edge_id)
        .filter_map(|o| {
            s.source_locations
                .iter()
                .find(|l| l.id == o.source_location_id)
                .map(|l| (l.start_line, l.start_col))
        })
        .collect()
}

#[test]
fn const_usage_emits_usage_edge_with_occurrence() {
    let s = index_src("pub const LIMIT: usize = 3;\npub fn f() -> usize { LIMIT }");
    let edge = edge_id_between(&s, EDGE_USAGE_T, "f", "LIMIT");
    assert!(
        edge.is_some(),
        "expected EDGE_USAGE f -> LIMIT, edges: {:?}",
        s.edges
    );
    let positions = edge_occurrence_positions(&s, edge.unwrap());
    assert_eq!(positions, vec![(2, 23)], "occurrence at the LIMIT token");
}

#[test]
fn type_in_signature_emits_type_usage_with_occurrence() {
    let s = index_src("pub struct S;\npub fn f(s: S) {}");
    let edge = edge_id_between(&s, EDGE_TYPE_USAGE_T, "f", "S");
    assert!(
        edge.is_some(),
        "expected EDGE_TYPE_USAGE f -> S, edges: {:?}",
        s.edges
    );
    let positions = edge_occurrence_positions(&s, edge.unwrap());
    assert_eq!(
        positions,
        vec![(2, 13)],
        "occurrence at the S token in the signature"
    );
}

#[test]
fn field_access_emits_usage_edge_to_field() {
    let s = index_src("pub struct P { pub x: i32 }\npub fn f(p: P) -> i32 { p.x }");
    let edge = edge_id_between(&s, EDGE_USAGE_T, "f", "P::x");
    assert!(
        edge.is_some(),
        "expected EDGE_USAGE f -> P::x, edges: {:?}",
        s.edges
    );
    assert_eq!(edge_occurrence_positions(&s, edge.unwrap()), vec![(2, 27)]);
}

#[test]
fn record_literal_field_emits_usage_edge() {
    let s = index_src("pub struct P { pub x: i32 }\npub fn make() -> P { P { x: 1 } }");
    let edge = edge_id_between(&s, EDGE_USAGE_T, "make", "P::x");
    assert!(
        edge.is_some(),
        "expected EDGE_USAGE make -> P::x, edges: {:?}",
        s.edges
    );
}

#[test]
fn call_edge_has_occurrence_at_call_site() {
    let s = index_src("pub fn bar() {}\npub fn foo() { bar(); }");
    let edge = edge_id_between(&s, EDGE_CALL, "foo", "bar");
    assert!(edge.is_some());
    assert_eq!(edge_occurrence_positions(&s, edge.unwrap()), vec![(2, 16)]);
}

#[test]
fn repeated_calls_record_one_edge_with_two_occurrences() {
    let s = index_src("pub fn bar() {}\npub fn foo() { bar(); bar(); }");
    let edge = edge_id_between(&s, EDGE_CALL, "foo", "bar").unwrap();
    let positions = edge_occurrence_positions(&s, edge);
    assert_eq!(positions.len(), 2, "one deduplicated edge, two occurrences");
}

#[test]
fn enum_variant_use_emits_usage_edge() {
    let s = index_src("pub enum E { A }\npub fn f() -> E { E::A }");
    let edge = edge_id_between(&s, EDGE_USAGE_T, "f", "E::A");
    assert!(
        edge.is_some(),
        "expected EDGE_USAGE f -> E::A, edges: {:?}",
        s.edges
    );
}

#[test]
fn struct_field_type_emits_type_usage_from_owner() {
    // The reference context of a field's type is the owning struct.
    let s = index_src("pub struct Inner;\npub struct Outer { pub i: Inner }");
    let edge = edge_id_between(&s, EDGE_TYPE_USAGE_T, "Outer", "Inner");
    assert!(
        edge.is_some(),
        "expected EDGE_TYPE_USAGE Outer -> Inner, edges: {:?}",
        s.edges
    );
}

// -----------------------------------------------------------------------
// Scope locations — full item extents for snippet display
// -----------------------------------------------------------------------

const LOCATION_SCOPE_T: i32 = 1;

fn scope_locations_of(s: &OwnedIntermediateStorage, name: &str) -> Vec<(u32, u32, u32, u32)> {
    let Some(node_id) = s
        .nodes
        .iter()
        .find(|n| decode_name(&n.serialized_name) == name)
        .map(|n| n.id)
    else {
        return Vec::new();
    };
    s.occurrences
        .iter()
        .filter(|o| o.element_id == node_id)
        .filter_map(|o| {
            s.source_locations
                .iter()
                .find(|l| l.id == o.source_location_id && l.type_ == LOCATION_SCOPE_T)
                .map(|l| (l.start_line, l.start_col, l.end_line, l.end_col))
        })
        .collect()
}

#[test]
fn function_gets_scope_location_spanning_the_body() {
    let s = index_src("pub fn f() {\n    let _x = 1;\n}");
    let scopes = scope_locations_of(&s, "f");
    assert_eq!(
        scopes,
        vec![(1, 1, 3, 2)],
        "scope spans fn signature through closing brace"
    );
}

#[test]
fn struct_gets_scope_location() {
    let s = index_src("pub struct S {\n    pub x: i32,\n}");
    let scopes = scope_locations_of(&s, "S");
    assert_eq!(scopes, vec![(1, 1, 3, 2)]);
}

#[test]
fn impl_method_gets_scope_location() {
    let s = index_src("pub struct S;\nimpl S {\n    pub fn m(&self) {\n    }\n}");
    let scopes = scope_locations_of(&s, "S::m");
    assert_eq!(scopes, vec![(3, 5, 4, 6)]);
}

// -----------------------------------------------------------------------
// Type-system edges (context/DESIGN_RUST_TYPE_SYSTEM_EDGES.md)
// -----------------------------------------------------------------------

const EDGE_OVERRIDE_T: i32 = 1 << 5;
const EDGE_TYPE_ARGUMENT_T: i32 = 1 << 6;

#[test]
fn where_clause_bound_attaches_to_the_parameter() {
    let s = index_src("pub trait Show {} pub fn dump<T>(val: T) where T: Show {}");
    assert!(
        has_edge(&s, EDGE_TYPE_USAGE, "dump::T", "Show"),
        "expected EDGE_TYPE_USAGE dump::T -> Show, edges: {:?}",
        s.edges
    );
}

#[test]
fn lifetime_outlives_emits_edge_between_lifetime_params() {
    let s = index_src("pub struct Pair<'a, 'b: 'a> { pub x: &'a u8, pub y: &'b u8 }");
    assert!(has_node(&s, "Pair::'a"), "nodes: {:?}", node_names(&s));
    assert!(has_node(&s, "Pair::'b"), "nodes: {:?}", node_names(&s));
    assert!(
        has_edge(&s, EDGE_TYPE_USAGE, "Pair::'b", "Pair::'a"),
        "expected outlives edge Pair::'b -> Pair::'a, edges: {:?}",
        s.edges
    );
}

#[test]
fn generic_argument_emits_type_argument_edge() {
    let s = index_src(
        "pub struct Foo;\npub struct Holder<T> { pub t: T }\npub fn f(h: Holder<Foo>) {}",
    );
    assert!(
        has_edge(&s, EDGE_TYPE_ARGUMENT_T, "f", "Foo"),
        "expected EDGE_TYPE_ARGUMENT f -> Foo, edges: {:?}",
        s.edges
    );
    // The outer generic type is ordinary type usage.
    assert!(has_edge(&s, EDGE_TYPE_USAGE, "f", "Holder"));
}

#[test]
fn impl_method_overrides_trait_method() {
    let s = index_src(
        "pub trait Draw { fn draw(&self); }\npub struct Circle;\nimpl Draw for Circle { fn draw(&self) {} }",
    );
    assert!(
        has_edge(&s, EDGE_OVERRIDE_T, "Circle::draw", "Draw::draw"),
        "expected EDGE_OVERRIDE Circle::draw -> Draw::draw, edges: {:?}",
        s.edges
    );
    assert!(has_edge(&s, EDGE_INHERITANCE, "Circle", "Draw"));
}

#[test]
fn bound_in_generic_args_is_type_argument_from_param() {
    // `T: Iterator<Item = Foo>` — Foo is a generic argument of the bound,
    // attributed to the parameter carrying it.
    let s = index_src(
        "pub struct Foo;\npub trait Gen { type Item; }\npub fn f<T: Gen<Item = Foo>>(t: T) {}",
    );
    assert!(
        has_edge(&s, EDGE_TYPE_ARGUMENT_T, "f::T", "Foo"),
        "expected EDGE_TYPE_ARGUMENT f::T -> Foo, edges: {:?}",
        s.edges
    );
    assert!(has_edge(&s, EDGE_TYPE_USAGE, "f::T", "Gen"));
}

#[test]
fn supertrait_with_qualified_path_resolves_exactly() {
    let s = index_src(
        "pub mod one { pub trait Marker {} }\npub mod two { pub trait Marker {} }\npub trait Special: two::Marker {}",
    );
    assert!(
        has_edge(&s, EDGE_INHERITANCE, "Special", "two::Marker"),
        "expected EDGE_INHERITANCE Special -> two::Marker, edges: {:?}",
        s.edges
    );
    assert!(!has_edge(&s, EDGE_INHERITANCE, "Special", "one::Marker"));
}

#[test]
fn impl_block_generic_params_attach_to_the_type() {
    let s = index_src(
        "pub trait Clean {} pub struct W<T> { pub t: T }\nimpl<T: Clean> W<T> { pub fn go(&self) {} }",
    );
    // The impl's own param bound attaches to the param node.
    assert!(
        has_edge(&s, EDGE_TYPE_USAGE, "W::T", "Clean"),
        "expected EDGE_TYPE_USAGE W::T -> Clean, edges: {:?}",
        s.edges
    );
}

// -----------------------------------------------------------------------
// Macro-expanded items (builtin derives expand without a proc-macro server)
// -----------------------------------------------------------------------

const DEFINITION_IMPLICIT_T: i32 = 1;

fn definition_kind_of(s: &OwnedIntermediateStorage, name: &str) -> Option<i32> {
    let node_id = s
        .nodes
        .iter()
        .find(|n| decode_name(&n.serialized_name) == name)
        .map(|n| n.id)?;
    s.symbols
        .iter()
        .find(|sym| sym.id == node_id)
        .map(|sym| sym.definition_kind)
}

#[test]
fn derive_clone_generates_implicit_method() {
    let s = index_src_with_sysroot("#[derive(Clone)]\npub struct P { pub x: i32 }");
    assert!(has_node(&s, "P::clone"), "nodes: {:?}", node_names(&s));
    assert_eq!(
        definition_kind_of(&s, "P::clone"),
        Some(DEFINITION_IMPLICIT_T),
        "derive-generated method must be IMPLICIT"
    );
    assert!(has_edge(&s, EDGE_MEMBER, "P", "P::clone"));
}

#[test]
fn derive_method_location_maps_to_the_attribute_site() {
    let s = index_src_with_sysroot("#[derive(Clone)]\npub struct P { pub x: i32 }");
    let node_id = s
        .nodes
        .iter()
        .find(|n| decode_name(&n.serialized_name) == "P::clone")
        .map(|n| n.id)
        .expect("P::clone node");
    let lines: Vec<u32> = s
        .occurrences
        .iter()
        .filter(|o| o.element_id == node_id)
        .filter_map(|o| {
            s.source_locations
                .iter()
                .find(|l| l.id == o.source_location_id)
                .map(|l| l.start_line)
        })
        .collect();
    assert_eq!(
        lines,
        vec![1],
        "location must map to the derive attribute on line 1"
    );
}

#[test]
fn call_to_derived_method_resolves() {
    let s = index_src_with_sysroot(
        "#[derive(Clone)]\npub struct P { pub x: i32 }\npub fn dup(p: &P) -> P { p.clone() }",
    );
    assert!(
        has_edge(&s, EDGE_CALL, "dup", "P::clone"),
        "expected EDGE_CALL dup -> P::clone, edges: {:?}",
        s.edges
    );
}

#[test]
fn multiple_derives_generate_all_methods() {
    let s =
        index_src_with_sysroot("#[derive(Clone, Debug, PartialEq)]\npub struct P { pub x: i32 }");
    assert!(has_node(&s, "P::clone"), "nodes: {:?}", node_names(&s));
    assert!(has_node(&s, "P::fmt"), "nodes: {:?}", node_names(&s));
    assert!(has_node(&s, "P::eq"), "nodes: {:?}", node_names(&s));
}

#[test]
fn handwritten_items_stay_explicit() {
    let s =
        index_src_with_sysroot("#[derive(Clone)]\npub struct P;\nimpl P { pub fn go(&self) {} }");
    assert_eq!(definition_kind_of(&s, "P"), Some(2), "struct is EXPLICIT");
    assert_eq!(
        definition_kind_of(&s, "P::go"),
        Some(2),
        "handwritten method is EXPLICIT"
    );
    assert_eq!(
        definition_kind_of(&s, "P::clone"),
        Some(DEFINITION_IMPLICIT_T)
    );
}

// -----------------------------------------------------------------------
// Import edges — `use` items
// -----------------------------------------------------------------------

const EDGE_IMPORT_T: i32 = 1 << 9;
const NODE_FILE_T: i32 = 1 << 18;

/// EDGE_IMPORT edges whose source is the indexed file's own node,
/// returning the decoded target names.
fn file_import_targets(s: &OwnedIntermediateStorage) -> Vec<String> {
    let file_ids: Vec<i64> = s
        .nodes
        .iter()
        .filter(|n| n.type_ == NODE_FILE_T)
        .map(|n| n.id)
        .collect();
    s.edges
        .iter()
        .filter(|e| e.type_ == EDGE_IMPORT_T && file_ids.contains(&e.source_node_id))
        .filter_map(|e| {
            s.nodes
                .iter()
                .find(|n| n.id == e.target_node_id)
                .map(|n| decode_name(&n.serialized_name))
        })
        .collect()
}

#[test]
fn use_item_emits_import_edge_from_module() {
    let s = index_src("pub mod a { pub fn f() {} } pub mod b { use crate::a::f; }");
    assert!(
        has_edge(&s, EDGE_IMPORT_T, "b", "a::f"),
        "expected EDGE_IMPORT from b to a::f, edges: {:?}",
        s.edges
    );
}

#[test]
fn grouped_and_aliased_imports_resolve_each_leaf() {
    let s = index_src(
        "pub mod a { pub fn f() {} pub struct G; } pub mod b { use crate::a::{f, G as H}; }",
    );
    assert!(
        has_edge(&s, EDGE_IMPORT_T, "b", "a::f"),
        "expected EDGE_IMPORT b -> a::f, edges: {:?}",
        s.edges
    );
    assert!(
        has_edge(&s, EDGE_IMPORT_T, "b", "a::G"),
        "expected EDGE_IMPORT b -> a::G (aliased), edges: {:?}",
        s.edges
    );
}

#[test]
fn qualified_top_level_import_attaches_to_file_node() {
    // The crate-root module is unnamed and has no node — the import edge
    // starts at the file's own node.
    let s = index_src("pub mod a { pub fn f() {} } use a::f;");
    assert_eq!(file_import_targets(&s), vec!["a::f".to_owned()]);
}

#[test]
fn glob_import_targets_the_module() {
    let s = index_src("pub mod a { pub fn f() {} } pub mod b { use crate::a::*; }");
    assert!(
        has_edge(&s, EDGE_IMPORT_T, "b", "a"),
        "expected EDGE_IMPORT b -> a for glob, edges: {:?}",
        s.edges
    );
}

#[test]
fn external_imports_drop_silently() {
    let s = index_src("use std::collections::HashMap;\npub fn f() {}");
    assert!(
        !edge_types(&s).contains(&EDGE_IMPORT_T),
        "external import must not emit an edge, edges: {:?}",
        s.edges
    );
}

#[test]
fn function_local_import_attaches_to_the_function() {
    let s = index_src("pub mod a { pub fn f() {} } pub fn g() { use crate::a::f; }");
    assert!(
        has_edge(&s, EDGE_IMPORT_T, "g", "a::f"),
        "expected EDGE_IMPORT g -> a::f, edges: {:?}",
        s.edges
    );
}

// -----------------------------------------------------------------------
// Cargo project-model options (project model v1)
// -----------------------------------------------------------------------

/// Index a temp crate that declares feature `x` gating `gated()`, with the
/// given options.
fn index_feature_fixture(options: CargoOptions) -> OwnedIntermediateStorage {
    let tmp = tempfile::tempdir().unwrap();
    let src_dir = tmp.path().join("src");
    std::fs::create_dir_all(&src_dir).unwrap();
    std::fs::write(
        tmp.path().join("Cargo.toml"),
        "[package]\nname = \"_idx\"\nversion = \"0.0.0\"\nedition = \"2021\"\n\n[features]\nx = []\n",
    )
    .unwrap();
    std::fs::write(
        src_dir.join("lib.rs"),
        "#[cfg(feature = \"x\")]\npub fn gated() {}\npub fn always() {}\n",
    )
    .unwrap();
    index_crate_with(tmp.path(), LoadProfile::FAST, options, |_| {})
}

#[test]
fn feature_gated_item_absent_by_default() {
    let s = index_feature_fixture(CargoOptions::default());
    assert!(has_node(&s, "always"), "nodes: {:?}", node_names(&s));
    assert!(
        !has_node(&s, "gated"),
        "feature-gated fn must be absent without the feature, nodes: {:?}",
        node_names(&s)
    );
}

#[test]
fn feature_gated_item_present_with_selected_feature() {
    let s = index_feature_fixture(CargoOptions {
        features: vec!["x".to_owned()],
        ..CargoOptions::default()
    });
    assert!(has_node(&s, "always"), "nodes: {:?}", node_names(&s));
    assert!(
        has_node(&s, "gated"),
        "feature-gated fn must appear with features=[x], nodes: {:?}",
        node_names(&s)
    );
}

#[test]
fn feature_gated_item_present_with_all_features() {
    let s = index_feature_fixture(CargoOptions {
        all_features: true,
        ..CargoOptions::default()
    });
    assert!(
        has_node(&s, "gated"),
        "feature-gated fn must appear with --all-features, nodes: {:?}",
        node_names(&s)
    );
}

// -----------------------------------------------------------------------
// Local symbols
// -----------------------------------------------------------------------

const LOCATION_LOCAL_SYMBOL_T: i32 = 3;

/// Sorted per-local-symbol occurrence counts (LOCAL_SYMBOL locations only).
fn local_symbol_occurrence_counts(s: &OwnedIntermediateStorage) -> Vec<usize> {
    let local_loc_ids: std::collections::HashSet<i64> = s
        .source_locations
        .iter()
        .filter(|l| l.type_ == LOCATION_LOCAL_SYMBOL_T)
        .map(|l| l.id)
        .collect();
    let mut counts: Vec<usize> = s
        .local_symbols
        .iter()
        .map(|ls| {
            s.occurrences
                .iter()
                .filter(|o| o.element_id == ls.id && local_loc_ids.contains(&o.source_location_id))
                .count()
        })
        .collect();
    counts.sort_unstable();
    counts
}

#[test]
fn distinct_locals_with_same_name_do_not_merge() {
    let s = index_src("pub fn a() { let v = 1; let _ = v; } pub fn b() { let v = 2; let _ = v; }");
    assert_eq!(
        s.local_symbols.len(),
        2,
        "same-named locals in sibling fns must stay distinct: {:?}",
        s.local_symbols
    );
    assert_eq!(local_symbol_occurrence_counts(&s), vec![2, 2]);
}

#[test]
fn local_decl_plus_two_uses_record_three_locations() {
    let s = index_src("pub fn f() { let x = 1; let y = x + x; let _ = y; }");
    // x: declaration + two uses; y: declaration + one use.
    assert_eq!(
        local_symbol_occurrence_counts(&s),
        vec![2, 3],
        "local symbols: {:?}",
        s.local_symbols
    );
}

#[test]
fn shadowing_creates_distinct_local_symbols() {
    let s = index_src("pub fn f() { let x = 1; let x = x + 1; let _ = x; }");
    assert_eq!(
        s.local_symbols.len(),
        2,
        "shadowed binding must be a fresh local symbol: {:?}",
        s.local_symbols
    );
    // each x: its declaration + one use
    assert_eq!(local_symbol_occurrence_counts(&s), vec![2, 2]);
}

#[test]
fn local_symbol_names_follow_cxx_position_convention() {
    let s = index_src("pub fn f() { let x = 1; let _ = x; }");
    assert_eq!(s.local_symbols.len(), 1);
    // C++ convention (CxxAstVisitorComponentIndexer::getLocalSymbolName):
    // fileName<line:col> of the declaration. `let x` puts x at line 1 col 18.
    let name = &s.local_symbols[0].name;
    assert!(
        name.ends_with("<1:18>"),
        "expected declaration-position suffix <1:18>, got {name}"
    );
}
