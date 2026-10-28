//! What a tool that changes the app reports of the state it led to.
//!
//! The app answers every read with the whole `UiState` (active nodes, both
//! graphs, the code view, every state machine with its context, tabs, search
//! matches), 5-10k tokens. After an action an agent mostly needs to know what
//! the action changed, so those tools report a [`StateForm`]: by default a
//! [`summary`] (app state, dialog, each machine's state without its context,
//! the active tab, the error counts, and a [`state_hash`] of the whole state)
//! plus the [`changes`] since the state last reported on that instance.
//! `ui_state` still returns everything.
//!
//! The baseline the changes are taken against is kept per instance
//! ([`StateBaseline`], on its [`crate::ipc::Bridge`]): the last state an agent
//! was given, by `ui_state` or by a call with a summary or the full state. A
//! call with `state: "none"` shows the agent nothing, so it leaves the baseline
//! where it was, and the next call's changes include what that one did.

use std::io::Write;

use serde_json::{json, Map, Value};

/// How much of the state a call that changes the app attaches to its result.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum StateForm {
    /// Nothing: no state, no changes.
    None,
    /// The [`summary`], and the [`changes`] since the last reported state.
    #[default]
    Summary,
    /// The whole `ui_state`, and the [`changes`].
    Full,
}

/// The whole state's top-level key that differs on every read without anything
/// having changed: the id of the request that read it.
const REQUEST_ID: &str = "request_id";

/// FNV-1a, 64 bit, fed through `io::Write` so a value serializes straight into
/// it. Stable across Rust versions and runs, unlike `DefaultHasher`, so a hash
/// means the same thing for as long as an agent holds on to it.
struct Fnv1a(u64);

impl Write for Fnv1a {
    fn write(&mut self, bytes: &[u8]) -> std::io::Result<usize> {
        for byte in bytes {
            self.0 = (self.0 ^ u64::from(*byte)).wrapping_mul(0x0000_0100_0000_01b3);
        }
        Ok(bytes.len())
    }

    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

/// A hash of the whole state, `request_id` left out: equal hashes mean nothing
/// in the state changed. 16 hex digits. serde_json's maps are ordered by key
/// (no `preserve_order` here), so the serialization it hashes is canonical.
pub fn state_hash(state: &Value) -> String {
    let mut hasher = Fnv1a(0xcbf2_9ce4_8422_2325);
    match state.as_object() {
        Some(fields) => {
            for (key, value) in fields.iter().filter(|(key, _)| *key != REQUEST_ID) {
                // Writing to the hasher cannot fail; a key and its value are
                // separated so ("ab", 1) and ("a", "b1") do not hash alike.
                let _ = write!(hasher, "{key:?}:");
                let _ = serde_json::to_writer(&mut hasher, value);
                let _ = hasher.write(b",");
            }
        }
        None => {
            let _ = serde_json::to_writer(&mut hasher, state);
        }
    }
    format!("{:016x}", hasher.0)
}

/// Each machine's state by its name, without the context; null from an app
/// before protocol v6, which has no machines.
fn machine_states(state: &Value) -> Value {
    match state["machines"].as_array() {
        Some(machines) => Value::Object(
            machines
                .iter()
                .filter_map(|m| Some((m["machine"].as_str()?.to_owned(), m["state"].clone())))
                .collect::<Map<_, _>>(),
        ),
        None => Value::Null,
    }
}

/// The active tab as `{ tab_id, title }`, or null when no tab is active.
fn active_tab(state: &Value) -> Value {
    state["tabs"]
        .as_array()
        .and_then(|tabs| tabs.iter().find(|t| t["active"] == true))
        .map_or(Value::Null, |t| json!({ "tab_id": t["tab_id"], "title": t["title"] }))
}

/// The summary of a whole `ui_state`: `{ app_state, dialog, machines: { name:
/// state }, active_tab: { tab_id, title } | null, error_total, error_fatal, hash }`.
pub fn summary(state: &Value) -> Value {
    json!({
        "app_state": state["app_state"],
        "dialog": state["dialog"],
        "machines": machine_states(state),
        "active_tab": active_tab(state),
        "error_total": state["error_total"],
        "error_fatal": state["error_fatal"],
        "hash": state_hash(state),
    })
}

/// `{ from, to }` when the summaries differ at `key`.
fn moved(previous: &Value, now: &Value, key: &str) -> Option<Value> {
    (previous[key] != now[key]).then(|| json!({ "from": previous[key], "to": now[key] }))
}

/// What changed from the `previous` summary to the one `now`; null without a
/// previous one (the first state reported on an instance has nothing to be
/// compared with). Always `hash_changed`; the rest only where something moved:
/// `app_state: { from, to }`, `machines: [ { machine, from, to } ]` (a machine
/// that appeared or went away has a null side), `dialog: { closed?, opened? }`,
/// `active_tab: { from, to }`, `errors: { from: { total, fatal }, to }`.
///
/// Taken between two reads, so a machine that moved and came back (an
/// activation: idle -> activating -> rendering -> idle) shows no transition;
/// `hash_changed` still says the state is not what it was.
pub fn changes(previous: Option<&Value>, now: &Value) -> Value {
    let Some(previous) = previous else {
        return Value::Null;
    };
    let mut changed = Map::new();
    changed.insert("hash_changed".into(), json!(previous["hash"] != now["hash"]));
    if let Some(app_state) = moved(previous, now, "app_state") {
        changed.insert("app_state".into(), app_state);
    }

    let empty = Map::new();
    let before = previous["machines"].as_object().unwrap_or(&empty);
    let after = now["machines"].as_object().unwrap_or(&empty);
    let mut names: Vec<&String> = before.keys().chain(after.keys()).collect();
    names.sort();
    names.dedup();
    let transitions: Vec<Value> = names
        .into_iter()
        .filter_map(|name| {
            let (from, to) = (before.get(name).unwrap_or(&Value::Null), after.get(name).unwrap_or(&Value::Null));
            (from != to).then(|| json!({ "machine": name, "from": from, "to": to }))
        })
        .collect();
    if !transitions.is_empty() {
        changed.insert("machines".into(), json!(transitions));
    }

    if previous["dialog"] != now["dialog"] {
        let mut dialog = Map::new();
        // "" is no dialog (null: an app that cannot tell).
        let shown = |d: &Value| d.as_str().is_some_and(|s| !s.is_empty());
        if shown(&previous["dialog"]) {
            dialog.insert("closed".into(), previous["dialog"].clone());
        }
        if shown(&now["dialog"]) {
            dialog.insert("opened".into(), now["dialog"].clone());
        }
        changed.insert("dialog".into(), Value::Object(dialog));
    }

    if let Some(tab) = moved(previous, now, "active_tab") {
        changed.insert("active_tab".into(), tab);
    }

    let errors = |s: &Value| json!({ "total": s["error_total"], "fatal": s["error_fatal"] });
    if errors(previous) != errors(now) {
        changed.insert("errors".into(), json!({ "from": errors(previous), "to": errors(now) }));
    }
    Value::Object(changed)
}

/// The state last reported on one instance: what [`changes`] are taken against.
#[derive(Debug, Default)]
pub struct StateBaseline {
    last: Option<Value>,
}

impl StateBaseline {
    /// Note a state the agent was given whole (`ui_state`): the next changes
    /// are taken against it.
    pub fn seen(&mut self, state: &Value) {
        self.last = Some(summary(state));
    }

    /// Put `state` into `result` in the given form: under `state` the summary or
    /// the whole state, under `changes` what changed since the last reported
    /// state (null the first time); nothing with [`StateForm::None`], which also
    /// leaves the baseline as it was.
    pub fn report(&mut self, result: &mut Map<String, Value>, state: Value, form: StateForm) {
        if form == StateForm::None {
            return;
        }
        let now = summary(&state);
        let changes = changes(self.last.as_ref(), &now);
        let shown = if form == StateForm::Full { state } else { now.clone() };
        self.last = Some(now);
        result.insert("state".into(), shown);
        result.insert("changes".into(), changes);
    }
}

/// What of the whole state `ui_state` returns when asked for less: the
/// top-level `fields` (all when empty), only the graph's edges that touch
/// `node` (a node id or a serialized name) and the nodes at their ends, and at
/// most `limit` items in each list, with `truncated` naming each cut list and
/// how long it was. A state for a big graph runs to megabytes otherwise.
pub fn select(mut state: Value, fields: &[String], node: Option<&str>, limit: Option<usize>) -> Value {
    if let Some(node) = node {
        keep_edges_of(&mut state, node);
    }

    if !fields.is_empty() {
        if let Value::Object(map) = &mut state {
            map.retain(|key, _| key == REQUEST_ID || fields.iter().any(|f| f == key));
        }
    }

    if let Some(limit) = limit {
        let mut truncated = Map::new();
        if let Value::Object(map) = &mut state {
            for (key, value) in map.iter_mut() {
                cut(key, value, limit, &mut truncated);
                if key == "graph" {
                    if let Value::Object(graph) = value {
                        for (part, list) in graph.iter_mut() {
                            cut(&format!("graph.{part}"), list, limit, &mut truncated);
                        }
                    }
                }
            }
            if !truncated.is_empty() {
                map.insert("truncated".into(), Value::Object(truncated));
            }
        }
    }
    state
}

/// Cuts `value`, if it is a list longer than `limit`, noting its length as `name`.
fn cut(name: &str, value: &mut Value, limit: usize, truncated: &mut Map<String, Value>) {
    if let Value::Array(list) = value {
        if list.len() > limit {
            truncated.insert(name.to_owned(), json!(list.len()));
            list.truncate(limit);
        }
    }
}

/// Leaves the graph with the edges whose source or target is `node` (its id, or
/// its serialized name), and the nodes they connect.
fn keep_edges_of(state: &mut Value, node: &str) {
    let Some(graph) = state.get_mut("graph").and_then(Value::as_object_mut) else {
        return;
    };
    let nodes = graph.get("nodes").and_then(Value::as_array).cloned().unwrap_or_default();
    // Edges name their ends by serialized name; a node id is looked up among the nodes.
    let name = match node.parse::<u64>() {
        Ok(id) => nodes
            .iter()
            .find(|n| n["node_id"].as_u64() == Some(id))
            .and_then(|n| n["serialized_name"].as_str())
            .unwrap_or_default()
            .to_owned(),
        Err(_) => node.to_owned(),
    };

    let edges: Vec<Value> = graph
        .get("edges")
        .and_then(Value::as_array)
        .map(|edges| {
            edges
                .iter()
                .filter(|e| !name.is_empty() && (e["source"] == name.as_str() || e["target"] == name.as_str()))
                .cloned()
                .collect()
        })
        .unwrap_or_default();
    let ends: std::collections::BTreeSet<&str> = edges
        .iter()
        .flat_map(|e| [e["source"].as_str(), e["target"].as_str()])
        .flatten()
        .chain(std::iter::once(name.as_str()))
        .collect();
    let nodes: Vec<Value> = nodes
        .into_iter()
        .filter(|n| n["serialized_name"].as_str().is_some_and(|s| ends.contains(s)))
        .collect();

    graph.insert("nodes".into(), Value::Array(nodes));
    graph.insert("edges".into(), Value::Array(edges));
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A whole state as the app reports it, with what the summary leaves out.
    fn ui_state(request_id: u64, project: &str, dialog: &str, tab: (u64, &str)) -> Value {
        json!({
            "request_id": request_id,
            "project_file_path": "/p/tutorial.srctrl.toml",
            "app_state": "Ready",
            "dialog": dialog,
            "indexing_active": false,
            "error_total": 0,
            "error_fatal": 0,
            "active_nodes": [ { "node_id": 7, "serialized_name": "::main", "display_name": "main", "node_kind": 4096 } ],
            "graph": { "nodes": [], "edges": [] },
            "code_view": null,
            "search_matches": [],
            "tabs": [
                { "tab_id": 1, "title": "Overview", "active": tab.0 == 1 },
                { "tab_id": tab.0, "title": tab.1, "active": true },
            ],
            "bookmarks": [],
            "machines": [
                { "machine": "app", "state": "running", "events": ["quit"], "context": { "startup_ms": 812 }, "revision": 2 },
                { "machine": "project", "state": project, "events": ["close"], "context": { "path": "/p" }, "revision": 5 },
            ],
            "view_graph": [],
        })
    }

    fn reported(baseline: &mut StateBaseline, state: Value, form: StateForm) -> Map<String, Value> {
        let mut result = Map::new();
        result.insert("ok".into(), json!(true));
        baseline.report(&mut result, state, form);
        result
    }

    #[test]
    fn the_summary_keeps_the_states_and_drops_the_rest() {
        let s = summary(&ui_state(3, "ready", "", (2, "main")));
        assert_eq!(
            s,
            json!({
                "app_state": "Ready",
                "dialog": "",
                "machines": { "app": "running", "project": "ready" },
                "active_tab": { "tab_id": 2, "title": "main" },
                "error_total": 0,
                "error_fatal": 0,
                "hash": s["hash"],
            })
        );
        assert_eq!(s["hash"].as_str().map(str::len), Some(16));
    }

    #[test]
    fn an_app_without_machines_or_tabs_summarizes_as_null() {
        let s = summary(&json!({ "app_state": "NoProject", "dialog": null, "tabs": [], "error_total": 0, "error_fatal": 0 }));
        assert_eq!(s["machines"], Value::Null);
        assert_eq!(s["active_tab"], Value::Null);
        assert_eq!(s["dialog"], Value::Null);
    }

    #[test]
    fn the_hash_ignores_the_request_id_and_nothing_else() {
        let a = ui_state(3, "ready", "", (2, "main"));
        assert_eq!(state_hash(&a), state_hash(&ui_state(4, "ready", "", (2, "main"))));
        // A change the summary does not show still changes the hash.
        let mut b = a.clone();
        b["machines"][0]["context"]["startup_ms"] = json!(813);
        assert_ne!(state_hash(&a), state_hash(&b));
        let mut c = a.clone();
        c["code_view"] = json!({ "file_path": "/p/main.cpp", "scroll_line": 1 });
        assert_ne!(state_hash(&a), state_hash(&c));
        // Stable: a known input hashes to a known value, whatever the build.
        assert_eq!(state_hash(&json!({ "request_id": 1 })), "cbf29ce484222325");
    }

    #[test]
    fn the_first_report_has_no_changes() {
        let mut baseline = StateBaseline::default();
        let r = reported(&mut baseline, ui_state(1, "ready", "", (2, "main")), StateForm::Summary);
        assert_eq!(r["changes"], Value::Null);
        assert_eq!(r["state"]["app_state"], "Ready");
        assert!(r["state"].get("active_nodes").is_none());
    }

    #[test]
    fn a_state_like_the_last_reports_no_change() {
        let mut baseline = StateBaseline::default();
        reported(&mut baseline, ui_state(1, "ready", "", (2, "main")), StateForm::Summary);
        let r = reported(&mut baseline, ui_state(2, "ready", "", (2, "main")), StateForm::Summary);
        assert_eq!(r["changes"], json!({ "hash_changed": false }));
    }

    #[test]
    fn changes_name_the_machines_dialog_tab_and_errors_that_moved() {
        let mut baseline = StateBaseline::default();
        reported(&mut baseline, ui_state(1, "ready", "", (2, "main")), StateForm::Summary);
        let mut now = ui_state(2, "loading", "Start Indexing", (3, "Foo"));
        now["error_total"] = json!(4);
        now["app_state"] = json!("Loading");
        now["machines"].as_array_mut().unwrap().push(json!({ "machine": "indexing", "state": "choosing_mode" }));
        let r = reported(&mut baseline, now, StateForm::Summary);
        assert_eq!(
            r["changes"],
            json!({
                "hash_changed": true,
                "app_state": { "from": "Ready", "to": "Loading" },
                "machines": [
                    { "machine": "indexing", "from": null, "to": "choosing_mode" },
                    { "machine": "project", "from": "ready", "to": "loading" },
                ],
                "dialog": { "opened": "Start Indexing" },
                "active_tab": { "from": { "tab_id": 2, "title": "main" }, "to": { "tab_id": 3, "title": "Foo" } },
                "errors": { "from": { "total": 0, "fatal": 0 }, "to": { "total": 4, "fatal": 0 } },
            })
        );
    }

    #[test]
    fn a_dialog_that_went_away_or_was_replaced_is_closed() {
        let base = |dialog| summary(&ui_state(1, "ready", dialog, (2, "main")));
        assert_eq!(changes(Some(&base("Start Indexing")), &base(""))["dialog"], json!({ "closed": "Start Indexing" }));
        assert_eq!(
            changes(Some(&base("'Question' (modal): Reindex?")), &base("Start Indexing"))["dialog"],
            json!({ "closed": "'Question' (modal): Reindex?", "opened": "Start Indexing" })
        );
    }

    #[test]
    fn a_change_only_the_whole_state_shows_is_the_hash_alone() {
        let a = ui_state(1, "ready", "", (2, "main"));
        let mut b = a.clone();
        b["active_nodes"] = json!([]);
        assert_eq!(changes(Some(&summary(&a)), &summary(&b)), json!({ "hash_changed": true }));
    }

    #[test]
    fn the_full_form_carries_the_whole_state_and_the_changes() {
        let mut baseline = StateBaseline::default();
        reported(&mut baseline, ui_state(1, "ready", "", (2, "main")), StateForm::Summary);
        let full = ui_state(2, "ready", "", (3, "Foo"));
        let r = reported(&mut baseline, full.clone(), StateForm::Full);
        assert_eq!(r["state"], full);
        assert_eq!(r["changes"]["active_tab"]["to"]["title"], "Foo");
    }

    #[test]
    fn none_attaches_nothing_and_keeps_the_baseline() {
        let mut baseline = StateBaseline::default();
        reported(&mut baseline, ui_state(1, "ready", "", (2, "main")), StateForm::Summary);
        let r = reported(&mut baseline, ui_state(2, "ready", "", (3, "Foo")), StateForm::None);
        assert_eq!(r.keys().collect::<Vec<_>>(), ["ok"]);
        // The next report's changes include what the unreported call did.
        let r = reported(&mut baseline, ui_state(3, "ready", "", (3, "Foo")), StateForm::Summary);
        assert_eq!(r["changes"]["active_tab"]["from"]["title"], "main");
    }

    #[test]
    fn a_state_seen_whole_is_the_next_baseline() {
        let mut baseline = StateBaseline::default();
        baseline.seen(&ui_state(1, "ready", "", (3, "Foo")));
        let r = reported(&mut baseline, ui_state(2, "ready", "", (3, "Foo")), StateForm::Summary);
        assert_eq!(r["changes"], json!({ "hash_changed": false }));
    }

    /// A state with a graph of three nodes and two edges, and long lists.
    fn graph_state() -> Value {
        let mut state = ui_state(3, "ready", "", (1, "Overview"));
        state["graph"] = json!({
            "nodes": [
                { "node_id": 1, "serialized_name": "::a", "display_name": "a", "node_kind": 4096 },
                { "node_id": 2, "serialized_name": "::b", "display_name": "b", "node_kind": 4096 },
                { "node_id": 3, "serialized_name": "::c", "display_name": "c", "node_kind": 4096 },
            ],
            "edges": [
                { "edge_id": 10, "edge_type": 8, "source": "::a", "target": "::b" },
                { "edge_id": 11, "edge_type": 8, "source": "::b", "target": "::c" },
            ],
        });
        state["search_matches"] = json!([{ "text": "x" }, { "text": "y" }, { "text": "z" }]);
        state
    }

    #[test]
    fn select_without_arguments_is_the_whole_state() {
        assert_eq!(select(graph_state(), &[], None, None), graph_state());
    }

    #[test]
    fn select_keeps_the_fields_asked_for() {
        let state = select(graph_state(), &["app_state".into(), "active_nodes".into()], None, None);
        let keys: Vec<&str> = state.as_object().unwrap().keys().map(String::as_str).collect();
        assert_eq!(keys.len(), 3, "{keys:?}");
        assert_eq!(state["app_state"], "Ready");
        assert_eq!(state["request_id"], 3);
    }

    #[test]
    fn select_keeps_a_nodes_edges_by_id_or_name() {
        for node in ["1", "::a"] {
            let state = select(graph_state(), &[], Some(node), None);
            assert_eq!(state["graph"]["edges"].as_array().unwrap().len(), 1, "{node}");
            assert_eq!(state["graph"]["edges"][0]["edge_id"], 10);
            let names: Vec<&str> =
                state["graph"]["nodes"].as_array().unwrap().iter().map(|n| n["serialized_name"].as_str().unwrap()).collect();
            assert_eq!(names, ["::a", "::b"]);
        }
        let middle = select(graph_state(), &[], Some("2"), None);
        assert_eq!(middle["graph"]["edges"].as_array().unwrap().len(), 2);
        assert_eq!(middle["graph"]["nodes"].as_array().unwrap().len(), 3);
    }

    #[test]
    fn select_cuts_lists_and_says_how_long_they_were() {
        let state = select(graph_state(), &[], None, Some(2));
        assert_eq!(state["search_matches"].as_array().unwrap().len(), 2);
        assert_eq!(state["graph"]["nodes"].as_array().unwrap().len(), 2);
        assert_eq!(state["truncated"]["search_matches"], 3);
        assert_eq!(state["truncated"]["graph.nodes"], 3);
        assert!(state["truncated"].get("graph.edges").is_none());
        assert!(state["truncated"].get("tabs").is_none());
    }
}
