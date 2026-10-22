//! The bridge's discovery catalog: what the `describe` tool serves.
//!
//! Tool descriptions stay one line ("when to use" + result shape) so a client
//! pays little for the whole surface up front; the detail lives here, one topic
//! per area, and an agent loads a topic only when it works in that area.
//!
//! Each topic names the tools it documents. That list is the single source of
//! the tool -> topic mapping: the server's tests check that every registered
//! tool belongs to exactly one topic and that every tool named here exists.

use serde_json::{json, Value};

/// One `describe` topic.
pub struct Topic {
    /// Topic key passed to `describe`.
    pub name: &'static str,
    /// One line for the index.
    pub summary: &'static str,
    /// MCP tools this topic documents.
    pub tools: &'static [&'static str],
    /// The detail: usage notes and exact result shapes.
    pub doc: &'static str,
}

/// Every topic, in the order the index lists them.
pub const TOPICS: &[Topic] = &[
    Topic {
        name: "overview",
        summary: "How to drive Sourcetrail: the usual loop, instances, and how results report failure.",
        tools: &["describe"],
        doc: "\
Loop: read state (ui_state) -> act (a project_*/nav_*/ui_* tool) -> the act returns the \
settled state or its own result -> repeat. Explore the widget tree with ui_snapshot when no \
semantic tool covers what you need.

First contact, end to end:
  instance_start    { bin: \"<path to Sourcetrail>\" }          -> { id: \"main-1a2b3c\" }
  project_open      { path: \"<project>.srctrlprj\" }           -> { ok, message }
  events_poll       { since_seq: 0 }   until AppStateChanged -> Ready
  nav_find          { query: \"MyClass\" }                       -> ranked matches
  nav_activate_node { node_id: <matches[0].node_ids[0]> }       -> the settled ui_state
  instance_stop     {}
With one running instance, `instance` can be left out everywhere; otherwise pass the id \
instance_start returned.

Instances: every tool except describe/instance_start/instance_list takes an `instance` id. \
Empty means the one running instance when exactly one is running; with none it is an app \
you started yourself on the unnamespaced st.agent.* channels, and with several it is \
ambiguous and the error names them. The bridge attaches lazily on first use. See \
describe('instances').

Failure reporting: transport or protocol failures (app not running, timeout, command \
rejected, no project loaded) come back as MCP tool errors. Resolution failures inside the \
UI (element not found, action unsupported, stale expect_hash) are data: { ok: false, message }. \
Unknown tool arguments are rejected, naming the field.

A question that blocks: Sourcetrail asks some questions (\"reindex the project?\") in a modal \
box that holds the thread commands run on until it is answered, so ui_state and nav_* time \
out while it is up. ui_frame, ui_snapshot, ui_query, ui_invoke and instance_handshake still \
answer (agent-control protocol v3+): ui_frame shows the box and names it in `windows`, and \
ui_query { query: \"$.roots..[?(@.role=='button' && @.name=='Cancel')]\" } finds its buttons \
(take the one whose rect is not empty; the box's own role reads \"unknown\"), which ui_invoke \
presses.

Settled state: tools that change the app (nav_activate_*, nav_scroll_to_line, nav_history) \
wait until the app reports Settled for the request, then return the ui_state object (shape \
in describe('state')).

Renamed tools: describe(<former name>) says what a tool is called now (get_ui_state -> \
ui_state, search -> nav_search, start_instance -> instance_start, ...).

Topics: describe() with no topic lists them all.",
    },
    Topic {
        name: "instances",
        summary: "Spawn, attach to, list, and stop Sourcetrail processes; protocol version check.",
        tools: &["instance_start", "instance_stop", "instance_list", "instance_handshake"],
        doc: "\
instance_start { bin, id?, project?, headless=true } -> { id }
  Spawns `bin` with --agent-instance <id> (channels st.agent.<id>.*) and returns once the app \
answers the handshake. id defaults to the binary's git label <branch>-<shorthash>, so two \
builds run side by side for comparisons. headless sets QT_QPA_PLATFORM=offscreen. `project` \
is loaded once the app is up. The app runs from its own directory, with stdin closed and its \
stdout/stderr in a console file under $SOURCETRAIL_MCP_HOME/logs (default \
<local app data>/sourcetrail-mcp/logs). If the app keeps running but never answers, a startup \
dialog is likely waiting invisibly: start it with headless: false to see it.

instance_stop { instance? } -> { killed: id }
  Kills a spawned instance (or forgets an attached one). Empty = the one running instance.

instance_list {} -> { instances: [ { id, spawned, pid, running, exit_code?, bin?, headless?, \
console_file? } | { id, running: false, start_failed } ] }
  A failed start stays listed with its reason; calls on that id repeat it instead of \
waiting on an app that never came up.

instance_handshake { instance? } -> { ok, bridge_protocol_version, app_protocol_version, \
app_version, app_build_id, bridge_build_id, instance_id, skew, wire }
  The connect-time GetInfo -> AppInfo exchange. skew: match | app_older (commands this \
bridge added are unknown to the app: rebuild the app) | app_newer (rebuild the bridge). \
wire: whether both sides were built against the same thoth-ipc.",
    },
    Topic {
        name: "state",
        summary: "The app's state: project, app_state FSM, active nodes, graph, code view, search matches, tabs.",
        tools: &["ui_state"],
        doc: "\
ui_state { instance? } -> {
  request_id, project_file_path,
  app_state: NoProject | Loading | Indexing | Ready | Busy,
  indexing_active, error_total, error_fatal,
  active_nodes: [ { node_id, serialized_name, display_name, node_kind } ],
  graph: { nodes: [node], edges: [ { edge_id, edge_type, source, target } ] } | null,
  code_view: { file_path, scroll_line } | null,
  search_matches: [ { text, node_kind, node_ids: [u64], score } ],
  tabs: [ { tab_id, title, active } ],
  dialog: string }

app_state is the FSM to key off: Ready means a project is loaded and not indexing, and \
every nav_* tool needs a project. dialog says what waits for an answer in front of the app: \
a modal window (\"'title' (modal): text\") or one of Sourcetrail's in-window dialogs by its \
title (\"Start Indexing\" after opening a project that has no index yet); \"\" when nothing \
does, null from an app before agent-control protocol v3. Check it when Ready does not \
behave like Ready.

active_nodes follow every activation, whoever made it: nav_activate_*, nav_history, a click \
in the graph. display_name is the qualified name, node_kind Sourcetrail's NodeKind bit \
(the same bit in search_matches). serialized_name is what nav_activate_node takes by name; \
node_id works too. code_view is the file the code view shows on its own (after \
nav_activate_file or nav_scroll_to_line, with scroll_line); null while it lists an activated \
node's references across files.",
    },
    Topic {
        name: "events",
        summary: "Await transitions (indexing done, node activated) instead of re-polling state.",
        tools: &["events_poll"],
        doc: "\
events_poll { since_seq=0, instance? } -> { events: [ { seq, timestamp_ms, type, ... } ], latest_seq }
  Pass latest_seq back as since_seq. A jump in seq beyond the returned events means some \
were dropped (the buffer holds 2048): poll more often.

Event types and their fields:
  AppStateChanged { state }            IndexingStarted {}
  IndexingProgress { files_done, files_total }
  IndexingFinished { succeeded }       ErrorCountChanged { total, fatal }
  NodesActivated { nodes: [node] }     EdgeActivated { edge }
  FileActivated { file_path }          SearchCompleted { matches }
  StatusChanged { text }               TabsChanged { tabs }
  FocusChanged { view }                CommandResult { request_id, ok, message }
  Settled { request_id }               AppInfo { request_id, protocol_version, ... }
  LogEvent { level, log_source, category, message, log_file, function, log_line } \
(only after logs_forward)",
    },
    Topic {
        name: "nav",
        summary: "Find code elements and navigate: search, activate nodes/files/tabs, scroll, back/forward.",
        tools: &[
            "nav_search",
            "nav_find",
            "nav_activate_node",
            "nav_activate_file",
            "nav_activate_tab",
            "nav_scroll_to_line",
            "nav_history",
        ],
        doc: "\
All of these need a loaded project (app_state other than NoProject); without one they fail \
with \"rejected: no project loaded\".

nav_search { query, instance? } -> [ { text, node_kind, node_ids: [u64], score } ]
  The app's autocompletion search over every node kind, unranked.

nav_find { query, instance? } -> { query, matches: [same as nav_search] }
  nav_search ranked for picking one: exact text, then prefix, then substring, then score. \
Activate matches[0].node_ids[0].

nav_activate_node { node_id? | serialized_name?, instance? } -> ui_state
  Shows the node in the graph and code view, like clicking it. node_id from a search match \
or ui_state; serialized_name from ui_state's active_nodes/graph.

nav_activate_file { path, instance? } -> ui_state       Opens a source file.
nav_activate_tab { tab_id, instance? } -> ui_state      tab_id from ui_state.tabs.
nav_scroll_to_line { path, line, instance? } -> ui_state
  Scrolls the code view to a line (1-based) of a file.
nav_history { direction: \"back\" | \"forward\", instance? } -> ui_state
  The toolbar's back/forward navigation.",
    },
    Topic {
        name: "ui",
        summary: "See and drive the UI: screenshots, the widget tree with refs, JSONPath queries, invoking actions.",
        tools: &["ui_frame", "ui_snapshot", "ui_query", "ui_invoke", "ui_capture"],
        doc: "\
For what no nav_* tool covers: menus, dialogs, buttons, preferences.

ui_frame { max_width=0, region?: { x, y, w, h }, instance? } -> PNG image + \
{ ok, frame: { width, height, format, bytes }, windows } | { ok: false, message }
  The screen as a user sees it: every visible top-level window composited at its position \
(dialogs and popups over the main window), so it is what to look at when ui_state does not \
explain what the app is doing. `windows` names them (\"'title' WxH (modal); ...\"). region \
crops in the full frame's pixels, then max_width downscales (aspect kept). Needs an app on \
agent-control protocol v2 or later (instance_handshake).

ui_snapshot { object_tree=false, instance? } -> { request_id, format, tree_hash, roots: [node] }
  node: { role, name, value, state, actions: [str], rect: { x, y, w, h }, \
ref: { object_name, path: [ { role, name, index } ] }, hash, children: [node] }
  The accessibility tree by default; object_tree=true gives the raw widget tree with \
properties. Pass a node's `ref` unchanged to ui_invoke / ui_capture.

ui_query { query, instance? } -> the ui_snapshot shape, roots = the matches
  JSONPath (RFC 9535) evaluated in the app over the accessibility tree, so only matches \
cross the wire. Example: $.roots..[?(@.role=='button' && @.name=='OK')].

ui_invoke { ref, action?, text?, expect_hash=0, instance? } -> { ok, message }
  Runs an accessible action from the node's `actions` (Press, Toggle, ShowMenu, ...) or \
sets an editable field's `text`. expect_hash (the node's `hash`) refuses the action if the \
element changed since the snapshot. Not found / unsupported -> ok: false.

ui_capture { ref, include_properties=false, expect_hash=0, instance? } -> PNG image + \
{ ok, frame: { width, height, format, bytes } } | { ok: false, message }
  One element, grabbed from its widget (a sub-element from its window, cropped).",
    },
    Topic {
        name: "project",
        summary: "Open a Sourcetrail project; indexing starts and app_state follows it.",
        tools: &["project_open"],
        doc: "\
project_open { path, instance? } -> { ok, message }
  Loads a project (.srctrlprj / .srctrl.toml) and returns once the app accepted it. A \
project with an up-to-date index is usable at once (app_state Ready). One without an index \
asks first: ui_state.dialog reads \"Start Indexing\", and nothing is indexed until its Start \
button is pressed:
  ui_query  { query: \"$.roots..[?(@.role=='button' && @.name=='Start')]\" }
  ui_invoke { ref: <roots[0].ref>, action: \"Press\" }
then wait for IndexingFinished with events_poll (app_state goes Indexing, then Ready).",
    },
    Topic {
        name: "logs",
        summary: "Forward the app's log lines into the event stream, filtered.",
        tools: &["logs_forward"],
        doc: "\
logs_forward { qt_rules?, event_pattern?, min_level=\"warning\", instance? } -> { ok, message }
  Turns on LogEvents in events_poll. min_level: info | warning | error. event_pattern: a \
regex over \"category: message\" (empty = everything at or above min_level). qt_rules: \
QLoggingCategory filter rules (\"qt.qpa.*=false\\nmy.category.debug=true\"), applied to the \
app's Qt logging; empty leaves it as it is. A bad regex is rejected and leaves the previous \
filter in place.",
    },
];

/// Topic names the catalog has also been asked for.
const TOPIC_ALIASES: &[(&str, &str)] = &[
    ("instance", "instances"),
    ("navigation", "nav"),
    ("log", "logs"),
];

/// Tools as they were called before the area prefixes, and what they are now.
pub const FORMER_NAMES: &[(&str, &str)] = &[
    ("get_ui_state", "ui_state"),
    ("get_instance_info", "instance_handshake"),
    ("poll_events", "events_poll"),
    ("set_log_filter", "logs_forward"),
    ("search", "nav_search"),
    ("find_element", "nav_find"),
    ("activate_node", "nav_activate_node"),
    ("activate_file", "nav_activate_file"),
    ("activate_tab", "nav_activate_tab"),
    ("get_snapshot", "ui_snapshot"),
    ("invoke_action", "ui_invoke"),
    ("query_ui", "ui_query"),
    ("capture_element", "ui_capture"),
    ("load_project", "project_open"),
    ("start_instance", "instance_start"),
    ("kill_instance", "instance_stop"),
    ("list_instances", "instance_list"),
];

/// What a former tool name is called now.
fn renamed(name: &str) -> Option<&'static str> {
    FORMER_NAMES.iter().find(|(old, _)| old.eq_ignore_ascii_case(name)).map(|(_, new)| *new)
}

/// The topic `name` asks for: its own name, the topic documenting a tool of
/// that name (current or former), an alias, or a near miss (case, a trailing
/// "s", a tool's area prefix).
///
/// Forgiving on purpose: the first thing a caller knows is a tool it saw in
/// tools/list (or used before the rename), not this catalog's topic names.
pub fn topic(name: &str) -> Option<&'static Topic> {
    let want = name.trim();
    let want = renamed(want).unwrap_or(want);
    let by_name = |n: &str| TOPICS.iter().find(move |t| t.name.eq_ignore_ascii_case(n));
    if let Some(t) = by_name(want) {
        return Some(t);
    }
    // A tool's own name names the topic that documents it.
    if let Some(t) = TOPICS.iter().find(|t| t.tools.iter().any(|n| n.eq_ignore_ascii_case(want))) {
        return Some(t);
    }
    if let Some(t) = TOPIC_ALIASES
        .iter()
        .find(|(alias, _)| alias.eq_ignore_ascii_case(want))
        .and_then(|(_, target)| by_name(target))
    {
        return Some(t);
    }
    // Singular/plural and a tool's area prefix ("nav_find" -> "nav").
    let norm = |s: &str| {
        let s = s.trim().to_ascii_lowercase();
        s.strip_suffix('s').map(str::to_owned).unwrap_or(s)
    };
    let want = norm(want);
    TOPICS
        .iter()
        .find(|t| norm(t.name) == want)
        .or_else(|| TOPICS.iter().find(|t| t.tools.iter().any(|n| norm(n.split('_').next().unwrap_or(n)) == want)))
}

/// The index `describe()` returns without a topic.
pub fn index() -> Value {
    let topics: Vec<Value> = TOPICS
        .iter()
        .map(|t| json!({ "topic": t.name, "summary": t.summary, "tools": t.tools }))
        .collect();
    json!({
        "topics": topics,
        "next": "describe(topic) returns that area's usage notes and exact result shapes.",
    })
}

/// What `describe(name)` returns. An unknown topic is data, not an error, so the
/// agent gets the valid names back in the same step.
pub fn describe(name: &str) -> Value {
    let name = name.trim();
    if name.is_empty() {
        return index();
    }
    match topic(name) {
        Some(t) => {
            let mut v = json!({ "topic": t.name, "summary": t.summary, "tools": t.tools, "doc": t.doc });
            if let Some(new) = renamed(name) {
                v["renamed"] = json!({ "from": name, "to": new });
            }
            v
        }
        None => json!({
            "ok": false,
            "message": format!("unknown topic '{name}'"),
            "topics": TOPICS.iter().map(|t| t.name).collect::<Vec<_>>(),
        }),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashSet;

    #[test]
    fn topic_names_are_unique() {
        let mut seen = HashSet::new();
        for t in TOPICS {
            assert!(seen.insert(t.name), "duplicate topic {}", t.name);
        }
    }

    #[test]
    fn a_reasonable_guess_finds_its_topic() {
        assert_eq!(topic("nav_find").map(|t| t.name), Some("nav"));
        assert_eq!(topic("ui_state").map(|t| t.name), Some("state"));
        assert_eq!(topic("instance_start").map(|t| t.name), Some("instances"));
        assert_eq!(topic("Instances").map(|t| t.name), Some("instances"));
        assert_eq!(topic("instance").map(|t| t.name), Some("instances"));
        assert_eq!(topic("navigation").map(|t| t.name), Some("nav"));
        // An exact topic name still wins over any fuzzy route.
        for t in TOPICS {
            assert_eq!(topic(t.name).map(|f| f.name), Some(t.name));
        }
        assert!(topic("wibble").is_none());
        for (alias, target) in TOPIC_ALIASES {
            assert!(topic(target).is_some(), "alias {alias} targets unknown topic {target}");
        }
    }

    #[test]
    fn every_former_name_leads_to_its_new_tool() {
        let tools: HashSet<&str> = TOPICS.iter().flat_map(|t| t.tools.iter().copied()).collect();
        for (old, new) in FORMER_NAMES {
            assert!(tools.contains(new), "{old} was renamed to {new}, which no topic documents");
            let t = topic(old).unwrap_or_else(|| panic!("describe('{old}') finds no topic"));
            assert!(t.tools.contains(new), "describe('{old}') lands on {} instead of {new}'s topic", t.name);
            assert_eq!(describe(old)["renamed"]["to"], *new);
        }
    }

    #[test]
    fn every_tool_belongs_to_one_topic() {
        let mut seen = HashSet::new();
        for t in TOPICS {
            assert!(!t.tools.is_empty(), "topic {} documents no tool", t.name);
            for tool in t.tools {
                assert!(seen.insert(*tool), "tool {tool} is in more than one topic");
            }
        }
    }

    #[test]
    fn every_topic_doc_mentions_its_tools() {
        for t in TOPICS {
            for tool in t.tools {
                if *tool != "describe" {
                    assert!(t.doc.contains(tool), "topic {} does not document {tool}", t.name);
                }
            }
        }
    }

    #[test]
    fn describe_without_topic_is_the_index() {
        let v = describe("");
        assert_eq!(v["topics"].as_array().unwrap().len(), TOPICS.len());
    }

    #[test]
    fn unknown_topic_lists_the_valid_ones() {
        let v = describe("nope");
        assert_eq!(v["ok"], false);
        assert!(v["topics"].as_array().unwrap().iter().any(|t| t == "nav"));
    }
}
