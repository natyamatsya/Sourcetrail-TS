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
  project_open      { path: \"<project>.srctrlprj\" }           -> { ok, state, changes }, Ready once loaded
  nav_find         { query: \"MyClass\" }                       -> ranked matches
  nav_activate_node { node_id: <matches[0].node_ids[0]> }       -> { ok, settled, state, changes }
  ui_state          {}                                          -> everything: active nodes, graph, code view
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
Unknown tool arguments are rejected, naming the field. When the app does not answer (stuck, \
busy past the limit, gone), the error's message ends with what is in progress, and its data \
is { app_silent_ms, in_progress: [ { machine, state, for_ms, for_ms_at_least? } ], machines: [ { machine, \
state, busy, for_ms, for_ms_at_least?, entered_by?: { from, event, source }, context?, \
children } ] }: the state machines as a tree (app > project > indexing, ...), as the bridge \
last saw them, which is current, since the app publishes every move.

A question that blocks: Sourcetrail asks some questions (\"reindex the project?\") in a modal \
box that holds the thread commands run on until it is answered, so ui_state and nav_* time \
out while it is up. The question is a state machine (protocol v6): machine_state { machine: \
\"question\" } shows its text and options, and machine_send { machine: \"question\", event: \
\"answer\", args: { label: \"Cancel\" } } answers it and closes the box; see \
describe('machines'). ui_frame, ui_snapshot, ui_query, ui_invoke and instance_handshake answer \
too (protocol v3+), for other boxes: ui_query { query: \"$.roots..[?(@.role=='button' && \
@.name=='Cancel')]\" } finds a button (take the one whose rect is not empty), which ui_invoke \
presses.

Settled state: tools that change the app (nav_activate_*, nav_scroll_to_line, nav_history, \
graph_*, ui_input) wait until the app reports Settled for the request, then return { ok, \
settled, state, changes }: by default `state` is a summary (app_state, dialog, each machine's \
state, the active tab, the error counts, a hash of the whole state) and `changes` what moved \
since the state last shown. state: \"full\" returns the whole ui_state instead, \"none\" \
neither; ui_state reads everything when the summary is not enough (describe('state')). Settled \
waits for the graph and code views to have drawn the result, their animations included (the \
activation machine, describe('machines')): a ui_frame taken right after shows it.

Renamed tools: describe(<former name>) says what a tool is called now (get_ui_state -> \
ui_state, search -> nav_search, start_instance -> instance_start, ...).

Topics: describe() with no topic lists them all.",
    },
    Topic {
        name: "instances",
        summary: "Spawn, attach to, list, and stop Sourcetrail processes; protocol version check.",
        tools: &["instance_start", "instance_stop", "instance_list", "instance_handshake"],
        doc: "\
instance_start { bin, id?, project?, headless=true, profile=\"agent\", fresh=false, debugger?, animations=false } -> { id }
  Spawns `bin` with --agent-instance <id> (channels st.agent.<id>.*) and returns once the app \
answers the handshake. id defaults to the binary's git label <branch>-<shorthash>, so two \
builds run side by side for comparisons. headless sets QT_QPA_PLATFORM=offscreen. `project` \
is loaded once the app is up. The app runs from its own directory, with stdin closed and its \
stdout/stderr in a console file under $SOURCETRAIL_MCP_HOME/logs (default \
<local app data>/sourcetrail-mcp/logs). If the app keeps running but never answers, a startup \
dialog is likely waiting invisibly: start it with headless: false to see it.
  profile: settings isolation. Spawned instances run on an isolated user-data directory \
(--user-data-dir: settings, window layout, logs, recent projects) that persists across \
runs, $SOURCETRAIL_MCP_HOME/profiles/<name>, \"agent\" unless profile names another; a \
profile that never ran starts from Sourcetrail's defaults. profile \"user\" runs on your own \
settings instead. fresh: true runs on a throwaway copy of the profile, deleted when the \
instance stops: a known starting point whose changes never persist.
  animations: the app runs with --no-animations unless this is true (the stored setting is \
left alone). Settling waits for the graph's transitions and the views' smooth scrolling too, \
so frames are the same either way; animations only make each navigation take longer (about a \
second).

instance_stop { instance?, kill?, timeout_s? } -> { stopped: id, how: quit|killed|forgotten|closed, \
exit_code?, note? }
  Asks a spawned instance to quit as closing its window would (machine app, event quit), so \
its teardown runs, and kills it when it does not exit within timeout_s (default 15) or kill \
is true; `note` says why it was killed. Forgets an attached instance. Empty = the one running \
instance.

instance_list {} -> { instances: [ { id, spawned, pid, running, exit_code?, bin?, headless?, \
console_file?, qt?, user_data_dir?, fresh? } | { id, running: false, start_failed } ] }
  A failed start stays listed with its reason; calls on that id repeat it instead of \
waiting on an app that never came up.

instance_handshake { instance? } -> { ok, bridge_protocol_version, app_protocol_version, \
app_version, app_build_id, bridge_build_id, instance_id, skew, wire }
  The connect-time GetInfo -> AppInfo exchange. skew: match | app_older (commands this \
bridge added are unknown to the app: rebuild the app) | app_newer (rebuild the bridge). \
wire: whether both sides were built against the same thoth-ipc.",
    },
    Topic {
        name: "debug",
        summary: "Crash dumps, live and post-mortem debugging through the platform debugger (cdb / lldb).",
        tools: &["instance_debug", "instance_debug_command", "instance_debug_dump"],
        doc: "\
Every instance instance_start launches gets the platform debugger attached right after the \
handshake (debugger: false skips it): cdb on Windows (Debugging Tools for Windows; CDB_PATH), \
lldb on macOS. On a crash (an exception nobody handled: access violation, abort/qFatal, stack \
overflow, an unhandled C++ exception) the debugger writes a full dump beside the instance's \
console file and holds the app stopped at the fault. instance_list then shows `stopped` (the \
exception and frame) and `dump_written`; every other tool on that instance fails at once with \
the stop, and a call already waiting says so when it times out.

instance_debug { instance?, pid?, break_in=false } -> { debugger, dump, dump_written, \
debugger_console, connect, stopped }
  Attaches if no debugger is attached yet (an app you started yourself: pass its pid). \
break_in: true stops the app now and returns the stop; resume it before driving the UI \
again, a stopped app answers nothing.

instance_debug_command { instance?, command } -> { output, stopped }
  A command in the debugger's own language. A running app is stopped for it and resumed \
after; a stopped one stays stopped. cdb: `.ecxr; kb` (the crash's stack), `~*k` (all \
threads), `!analyze -v`, `.frame N; dv` (locals), `bp module!Class::method`, `g` (resume, on \
its own). lldb: `bt all`, `frame select N`, `frame variable`, `breakpoint set -n \
Class::method`, `process continue`. Symbols: a build with debug information (e.g. the \
reldbg preset) names Sourcetrail's own frames; a Release build shows bare offsets.

instance_debug_dump { path, id? } -> { id, debugger, dump, stopped }
  Opens a dump post-mortem (an earlier instance's `dump`, or one from elsewhere) as an id for \
instance_debug_command; instance_stop closes it.

`connect` is for a human alongside: `cdb -remote <connect>`, or lldb's MCP server. Dumps are \
pruned at instance_start to the 3 newest (SOURCETRAIL_MCP_KEEP_DUMPS; 0 keeps all), never one \
an instance or an open dump points at.",
    },
    Topic {
        name: "state",
        summary: "The app's state: project, app_state FSM, active nodes, graph, code view, search matches, tabs; the summary and changes tools that act return.",
        tools: &["ui_state"],
        doc: "\
ui_state { instance?, fields?: [string], node?: string, limit?: n } -> {
  request_id, project_file_path,
  app_state: NoProject | Loading | Indexing | Ready | Busy,
  indexing_active, error_total, error_fatal,
  active_nodes: [ { node_id, serialized_name, display_name, node_kind } ],
  graph: { nodes: [node], edges: [ { edge_id, edge_type, source, target } ] } | null,
  code_view: { file_path, scroll_line } | null,
  search_matches: [ { text, node_kind, node_ids: [u64], score } ],
  tabs: [ { tab_id, title, active } ],
  bookmarks: [ { id, name, comment, category } ],
  dialog: string,
  view_graph: [ { kind: node | bundle | group, id, name, node_kind, parent?, active?, \
expanded?, hidden_members?, bundled?, splittable?, group? } ] }

Less of it: fields keeps those top-level fields (request_id always), e.g. [\"app_state\", \
\"active_nodes\"]; node keeps the graph's edges that touch that node (its node_id or \
serialized_name) and the nodes at their ends, to check one symbol's relations; limit keeps at \
most that many items in each list (graph.nodes, graph.edges, view_graph, search_matches, ...) \
and adds truncated: { \"graph.nodes\": 11719, ... } with the lengths of the lists it cut. For \
a big graph the whole state runs to megabytes.

app_state is the FSM to key off. NoProject -> Loading (a load was requested, by \
project_open or in the app, until the project is open and its first tab has shown the \
overview) -> Ready; Indexing while indexing, then Loading again while the new index loads and \
the first tab opens. Loading also covers the app starting and a refresh still being decided \
(the mode choice or a question about to come up). It is derived from the app, project and \
indexing machines (describe('machines')). nav_* needs a project and is refused while Loading. Ready means usable: nothing of an \
earlier load or indexing run is still on its way (agent-control protocol v4+). dialog says what waits for an answer in front of the app: \
a modal window (\"'title' (modal): text\") or one of Sourcetrail's in-window dialogs by its \
title (\"Start Indexing\" after opening a project that has no index yet); \"\" when nothing \
does, null from an app before agent-control protocol v3. Check it when Ready does not \
behave like Ready.

active_nodes follow every activation, whoever made it: nav_activate_*, nav_history, a click \
in the graph. display_name is the qualified name, node_kind Sourcetrail's NodeKind bit \
(the same bit in search_matches). serialized_name is what nav_activate_node takes by name; \
node_id works too. code_view is the file the code view shows on its own (after \
nav_activate_file or nav_scroll_to_line, with scroll_line); null while it lists an activated \
node's references across files.

graph is what the storage relates to the active nodes; view_graph is the graph as the view \
draws it, and what the graph_* tools act on (describe('graph')): members inside their node \
(parent: the index of that node in view_graph), and the bundles and groups the graph made.

The state a tool that acts returns (project_open, nav_activate_*, nav_scroll_to_line, \
nav_history, graph_*, ui_input): the whole ui_state is 5-10k tokens, so they take \
state: \"summary\" (the default) | \"full\" | \"none\" and return { ok, settled, state, changes }:
  state, summary: { app_state, dialog, machines: { <machine>: <state> }, \
active_tab: { tab_id, title } | null, error_total, error_fatal, hash }
    Each machine's state without its context or events (machine_state has those). hash is \
a hash of the whole ui_state (request_id left out): equal hashes, nothing changed. It covers \
the machines' contexts and revisions too, so every activation changes it, even one that comes \
back to a view shown before.
  state, full: the whole ui_state as above.
  state, none: neither state nor changes.
  changes: what moved since the state last shown on this instance (by ui_state, or by a \
call with a summary or the full state; a call with none moves nothing), null on the first: \
{ hash_changed, app_state?: { from, to }, machines?: [ { machine, from, to } ], \
dialog?: { closed?, opened? }, active_tab?: { from, to }, errors?: { from: { total, fatal }, \
to } }. Only what moved is there: { hash_changed: false } is no change at all; \
{ hash_changed: true } alone is a change the summary does not show (active nodes, graph, code \
view): read ui_state for it. Taken between two reads, so a machine that moved and came back \
(an activation: idle -> ... -> idle) is no transition; events_history has every move.
  Example, nav_activate_node (the tab's title follows the active node): { ok: true, \
settled: true, state: { app_state: \"Ready\", dialog: \"\", machines: { activation: \"idle\", \
app: \"running\", bookmark: \"idle\", project: \"ready\", ... }, active_tab: { tab_id: 10, \
title: \"the_central_hub\" }, error_total: 0, error_fatal: 0, hash: \"ae09456a00e59bb8\" }, \
changes: { hash_changed: true, active_tab: { from: { tab_id: 10, title: \"overview\" }, to: \
{ tab_id: 10, title: \"the_central_hub\" } } } }. A click on the bookmark button then: \
changes: { hash_changed: true, dialog: { opened: \"QtBookmarkCreator\" }, machines: [ { \
machine: \"bookmark\", from: \"idle\", to: \"editing\" } ] }.",
    },
    Topic {
        name: "events",
        summary: "Await transitions (indexing done, node activated) instead of re-polling state.",
        tools: &["events_poll", "events_history"],
        doc: "\
events_poll { since_seq=0, instance? } -> { events: [ { seq, timestamp_ms, type, caused_by?, ... } ], latest_seq }
  Pass latest_seq back as since_seq. A jump in seq beyond the returned events means some \
were dropped (the buffer holds 2048): poll more often. caused_by (agent-control protocol v7) \
is the request_id of the command the event follows from: what its handling dispatched, and \
the state machine episodes it set going (an activation until the views have drawn, an \
indexing run until idle). Absent: the user did it, or the app on its own.

events_history { request_id?, last=10, instance? } -> { requests: [ { request_id, command, \
sent_ms, ack: { ok, message, after_ms }, settled_after_ms, events: [ { seq, after_ms, type, \
machine?, from?, to?, event?, nodes?, dialog?, ... } ] } ] }
  What the commands this bridge sent led to, matched by caused_by: which nodes an activation \
showed, which machines moved, a dialog a click opened, and how long until it settled. For \
one request, or the last ones; reads (ui_state, frames, queries) lead to nothing and are left \
out. From the events the bridge has read (the last 2048).

Event types and their fields:
  AppStateChanged { state }            IndexingStarted {}
  IndexingProgress { files_done, files_total }
  IndexingFinished { succeeded }       ErrorCountChanged { total, fatal }
  NodesActivated { nodes: [node] }     EdgeActivated { edge }
  FileActivated { file_path }          SearchCompleted { matches }
  StatusChanged { text }               TabsChanged { tabs }
  FocusChanged { view }                CommandResult { request_id, ok, message }
  Settled { request_id }               AppInfo { request_id, protocol_version, ... }
  MachineTransition { machine, from, to, event, source, context, revision } (describe('machines'))
  DialogChanged { dialog }             a dialog came up (as ui_state.dialog) or went away (\"\")
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

The ones that act return once the app settled, as { ok, settled, state, changes }: state \
is a summary unless state: \"full\" (the whole ui_state) or \"none\" (neither); changes are \
what moved since the state last shown. describe('state') has both shapes; ui_state reads \
everything, e.g. which nodes are active.

nav_activate_node { node_id? | serialized_name?, state=\"summary\", instance? } -> { ok, settled, state, changes }
  Shows the node in the graph and code view, like clicking it. node_id from a search match \
or ui_state; serialized_name from ui_state's active_nodes/graph.

nav_activate_file { path, state?, instance? } -> the same       Opens a source file.
nav_activate_tab { tab_id, state?, instance? } -> the same      tab_id from ui_state.tabs.
nav_scroll_to_line { path, line, state?, instance? } -> the same
  Scrolls the code view to a line (1-based) of a file.
nav_history { direction: \"back\" | \"forward\", state?, instance? } -> the same
  The toolbar's back/forward navigation.",
    },
    Topic {
        name: "graph",
        summary: "Work the graph as drawn: expand members, hide nodes, split bundles, show trails.",
        tools: &["graph_expand", "graph_hide", "graph_split", "graph_trail"],
        doc: "\
The graph's own controls, on the current tab's graph (agent-control protocol v7). Each returns \
{ ok, settled, state, changes } once the graph has drawn the result, state a summary unless \
state: \"full\" (the whole ui_state) or \"none\" (describe('state')); ids come from ui_state.view_graph, \
which lists what the view draws: symbols (kind node; members carry parent, the index of their \
node), and the bundles (\"12 callers\") and groups (by file, namespace...) it made.

graph_expand { node_id, expand=true, state?, instance? } -> { ok, settled, state, changes }
  A node whose view_graph entry has `expanded` has an expand toggle: true shows its members \
(hidden_members says how many more), false collapses them.
graph_hide { node_id, state?, instance? } -> { ok, settled, state, changes }
  Takes a node (or an edge, by its id from ui_state.graph) out of the graph.
graph_split { bundle_id, state?, instance? } -> { ok, settled, state, changes }
  Splits a bundle or group whose entry is splittable, as clicking it does. A file or \
namespace group is not split: its id is that node's, so nav_activate_node shows it.
graph_trail { node_id, direction=\"outgoing\" | \"incoming\", target_id?, edge_kinds?, \
node_kinds?, depth=0, layout?, include_non_indexed=false, state?, instance? } -> { ok, settled, state, changes }
  The trail buttons and the custom trail dialog. outgoing shows what a function calls, the \
classes derived from a class, what a file includes; incoming a function's callers, a class's \
bases, a file's includers. edge_kinds default by the node's kind (functions: call, override; \
classes: inheritance, template specialization; files: include); name them for other nodes. \
target_id: the paths from node_id to it instead. depth 0 is as deep as it goes. A wrong kind \
name is refused with the list of valid ones.",
    },
    Topic {
        name: "ui",
        summary: "See and drive the UI: screenshots, the widget tree with refs, JSONPath queries, invoking actions.",
        tools: &["ui_frame", "ui_snapshot", "ui_query", "ui_invoke", "ui_capture"],
        doc: "\
For what no nav_* tool covers: menus, dialogs, buttons, preferences.

ui_frame { max_width=0, region?: { x, y, w, h }, instance? } -> PNG image + \
{ ok, frame: { width, height, format, bytes, origin: { x, y }, scale }, windows } | { ok: false, message }
  The screen as a user sees it: every visible top-level window composited at its position \
(dialogs and popups over the main window), so it is what to look at when ui_state does not \
explain what the app is doing. `windows` names them (\"'title' WxH (modal); ...\"). region \
crops in the full frame's pixels, then max_width downscales (aspect kept). origin and scale \
place the frame on screen: pixel p is the global point origin + p / scale, which is how \
ui_input { at_frame } aims at what the frame shows (v5+). Needs an app on agent-control \
protocol v2 or later (instance_handshake).

ui_snapshot { object_tree=false, instance? } -> { request_id, format, tree_hash, roots: [node] }
  node: { role, name, value, state, actions: [str], rect: { x, y, w, h }, \
ref: { object_name, path: [ { role, name, index } ] }, hash, children: [node] }
  The accessibility tree by default; object_tree=true gives the raw widget tree with \
properties. Pass a node's `ref` unchanged to ui_invoke / ui_capture.
  A ref is { object_name, path }. object_name (the anchor) is the widget carrying that \
name anywhere in the app, and path steps down from it; without one, path starts at a \
top-level window. The snapshot anchors each ref on the nearest widget whose name exactly one \
visible widget carries, so most refs are short and survive a window title change, e.g. the \
toolbar's Back button: { object_name: \"undo_button\", path: [] }. A known name works by hand \
too: ui_invoke { ref: { object_name: \"home_button\" }, action: \"Press\" }. A name several \
visible widgets carry is refused, saying so.

ui_query { query, instance? } -> the ui_snapshot shape, roots = the matches
  JSONPath (RFC 9535) evaluated in the app over the accessibility tree, so only matches \
cross the wire. Example: $.roots..[?(@.role=='button' && @.name=='OK')]; filters reach \
nested members too: [?(@.ref.object_name=='search_button')], [?(@.rect.w > 600)]. Matches carry \
their state bits as in ui_snapshot (checked, disabled, ...); a match comes without its \
children (children: [] even when it has some): select them with .children[*], e.g. \
$.roots..[?(@.role=='list' && @.name=='Targets')].children[*] for a list's items (Toggle \
checks a checkable one).

ui_invoke { ref, action?, text?, expect_hash=0, instance? } -> { ok, message, drawn? }
  Runs an accessible action from the node's `actions` (Press, Toggle, ShowMenu, ...) or \
sets an editable field's `text`; on a combo box without one, `text` picks the item it names, \
as choosing it from the list does (no such item: ok: false, listing them). expect_hash (the node's `hash`) refuses the action if the \
element changed since the snapshot. Not found / unsupported / a disabled button -> ok: \
false; so is an element outside the modal window in front (a question box), as a person's \
clicks are: answer that first. An action that \
started an activation (Back, Forward, Home, a search result) returns once the views have \
drawn it: drawn: true (false: not within 5 s).

ui_capture { ref, include_properties=false, expect_hash=0, instance? } -> PNG image + \
{ ok, frame: { width, height, format, bytes } } | { ok: false, message }
  One element, grabbed from its widget (a sub-element from its window, cropped).",
    },
    Topic {
        name: "input",
        summary: "Synthetic mouse and keyboard: click graph nodes, hover, drag, zoom, type, open context menus.",
        tools: &["ui_input"],
        doc: "\
For what has no accessible action or nav_* tool, above all the graph: its nodes and edges \
are drawn, not widgets, so they appear in no ui_snapshot; find them in ui_frame instead.

ui_input { ref? | at?: [x, y] | at_frame?: [px, py], gesture? | events?, state=\"summary\", instance? } -> \
{ ok, message, settled, state, changes } | { ok, message, settled: false, note } | { ok: false, message }
  Target, exactly one of:
    ref        an element from ui_snapshot / ui_query (a scroll area such as the graph view \
               gets the input on its viewport)
    at         the widget under a global point (ui_snapshot rects are global)
    at_frame   the widget under a pixel of the last ui_frame: what you see is where it goes. \
               ui_frame reports frame.origin and frame.scale; global = origin + pixel / scale.
  gesture: click | double_click | right_click | hover, at the target's center. right_click \
opens the context menu (graph nodes: open in new tab, hide, bookmark...); find its items \
with ui_query and press them with ui_invoke.
  events, in order (a drag: press, move with buttons [\"left\"], ..., release):
    { kind: move|press|release|doubleclick|wheel|keypress|keyrelease,
      x, y (relative to a ref element; global for at; frame pixels for at_frame) or centered: true,
      button: left|right|middle|none, buttons: [held during a move/wheel],
      modifiers: [shift, ctrl, alt, meta], wheel_delta: 120 per notch up,
      key: Enter|Escape|Tab|Left|F5|\"a\"|<Qt::Key code>, text }
  The graph zooms with ctrl + wheel and pans with a left drag on empty space.
  Docks move and resize as a person's mouse moves them. A drag on a card's grip (the dots, \
dock_grip, left of its title) lifts it: over another card, the drop indicators show; the \
release over one docks it there (the center one tabs it in). A drag on a gutter between cards \
(dock_gutter) resizes them. A drag may stop before its release: ui_frame then shows the \
indicators, and the release in a later ui_input goes where the drag is (the widget that grabbed \
the mouse), wherever it is aimed. The docks machine says where the docks went \
(describe('machines')), and arranges them by name too.
  While a modal window is in front (a question box), input for anything outside it is \
refused (ok: false), as a person's is.
  The result: `message` names the widget that received the input; once its effects \
drained, `state` is the state they led to and `changes` what moved since the state last shown: \
a summary by default (a dialog the click opened is changes.dialog.opened, a tab it switched \
changes.active_tab), state: \"full\" for the whole ui_state (a clicked node is in active_nodes), \
\"none\" for neither; describe('state'). settled: false means they did not drain in time, e.g. \
the click opened a dialog that holds the app: look with ui_frame.
  Modifiers travel with each event; code that reads the global keyboard state instead does \
not see them. Needs agent-control protocol v5.",
    },
    Topic {
        name: "project",
        summary: "Open a Sourcetrail project; indexing starts and app_state follows it.",
        tools: &["project_open"],
        doc: "\
project_open { path, state=\"summary\", instance? } -> { ok, state, changes } | { app_state: \"Loading\", question, note }
  Loads a project (.srctrlprj / .srctrl.toml) and returns once the load is done, so the \
state it returns is one to navigate from; instance_start { project } waits the same way. \
state is a summary unless state: \"full\" (the whole ui_state) or \"none\" (describe('state')). \
The app reports app_state Loading from the request until the project is open and the tab \
the load opens has shown the overview, and refuses nav_* meanwhile (\"still loading\"), so \
nothing of the load lands after a first navigation. A project with an up-to-date index \
comes back Ready. One without an index comes back Ready with the indexing machine in \
choosing_mode (state.dialog \"Start Indexing\"), and nothing is indexed until it starts:
  machine_send { machine: \"indexing\", event: \"start\", until: [\"idle\"], timeout_s: 300 }
returns once indexing is over and the project has reopened on the new index (app_state \
Ready), so navigation can follow at once. describe('machines') has the whole lifecycle.
  A load that asks first (the index is outdated or from another version: \"reindex?\") \
returns at once with { app_state: \"Loading\", question, note } (protocol v6): answer it with \
machine_send { machine: \"question\", event: \"answer\", args: { label: \"Reindex\" } }.
  A person's load (the start screen, Recent Projects: also when an agent clicks them) \
restores the tabs of the project's last session (kept with its bookmarks: what each tab \
showed, its recent history for Back, the tab in front; what is gone since is left out) \
instead of the overview, and refreshes directly (describe('machines')). project_open \
starts from the overview, as above.",
    },
    Topic {
        name: "machines",
        summary: "State machines: app startup, the project's load, indexing, questions, what the views show, the search box, the bookmark editor, the project wizard, the preferences, the docks.",
        tools: &["machine_state", "machine_send"],
        doc: "\
What waits for a person in Sourcetrail is a state machine: indexing (choose a mode, follow \
progress, decide on the report) and the questions the app asks; so are the app's startup and \
the project's load, which app_state is derived from, and an activation until the views have \
drawn it. A dialog shows a state; its \
buttons send the machine events; an agent sends the same events, and the dialog closes when \
an agent answered it. Every move is a MachineTransition event (events_poll) with its source: \
ui (a button), agent, or app (the pipeline moving on). Needs agent-control protocol v6.

machine_state { machine?, until?: [state], timeout_s=60, instance? }
  -> { machines: [ { machine, state, events, context, revision } ] }
  -> with until: { machine, transitions, reached }
  `events` are those the machine accepts now; `context` is what the state is about. ui_state \
carries the same list as `machines`.

machine_send { machine, event, args?: {...}, until?: [state], timeout_s=60, instance? }
  -> { ok, message, machine, transitions } (+ reached with until)
  A refused event is ok: false with the machine as it is (its state and accepted events). \
`transitions` are the moves seen meanwhile (machine_state's too); a move of the machine itself \
carries only the context fields that differ from `machine`'s (the machine as it is now; a field \
it has gained since reads null), other machines' moves their whole context. With until the \
call returns once the machine is \
in one of those states, or after timeout_s (at most 300; the call holds the bridge meanwhile) \
with reached: false. It also ends, reached: false, when the app asks a question meanwhile: \
the question's move is among the transitions; answer it (see question) and wait again. A \
run's end (indexing idle) reopens the project on the new index, so \
until: [\"idle\"] on indexing also waits for the project to be ready again; its moves are \
among the transitions.

indexing: idle -> pending -> choosing_mode -> pending -> preparing -> indexing -> finishing \
-> reporting -> finishing -> idle
  refresh { mode?: updated|all, direct?: bool }   idle: asks for a refresh. The mode choice \
opens (choosing_mode), or the app asks a question first (the index is outdated: see \
question), or finds nothing to do and returns to idle with context.outcome saying why. \
direct: true refreshes as a person's Refresh (the menu, Cmd+R, the search bar's button) \
does: no mode choice where one mode does (an update, or only \"all\" for an empty, \
out-of-version or corrupted index), so idle -> pending -> preparing at once, or back to idle \
with outcome \"Nothing to refresh, all files are up-to-date.\"; an update runs behind the \
status bar's progress. When the project's settings changed, the choice opens with \
context.reason saying so (all files preselected) instead of a question before it. A \
person's project load (the start screen, Recent Projects) leaves the same direct refresh; \
project_open (an agent's load) keeps the mode choice.
  select_mode { mode }             choosing_mode: updated | incomplete | all, among \
context.modes; context.files_to_index follows once counted (context.counting meanwhile).
  start { mode? }                  choosing_mode: indexes (with the selected mode by default).
  cancel                           choosing_mode: closes the choice.
  interrupt                        preparing/indexing: stops; what is indexed so far is kept \
for the report.
  keep | discard | reindex         reporting: the decision on the run's data, asked only of \
an interrupted run (its Keep/Discard dialog). A run that finished is kept without asking: \
reporting passes straight on (source app), and its summary is the status bar's text \
(\"Finished indexing: N source files indexed; time; errors\"). Offscreen (headless) an \
interrupted run is kept too.
  context: modes, mode, reason?, counting, files_to_index, files_to_clear (choosing_mode); progress \
{ started, finished, total, file }, interrupted (preparing..finishing); report { indexed, \
total_indexed, completed, total, seconds, errors, fatal, interrupted } (reporting); outcome \
(how the last request or run ended: kept, discarded, canceled, nothing to refresh...); \
changed_files (idle: indexed files changed on disk since, by the rule an update uses; the \
status bar's \"N files changed \u{2013} Update\" button, object name changed_files, runs \
the direct update, and the search bar's Refresh shows a dot meanwhile). The app checks \
when its window comes to the front and after an activation (at most every 10 s).
  Recipe: index a project that has none, and wait for it:
    machine_send { machine: \"indexing\", event: \"start\", until: [\"idle\"], timeout_s: 300 }
  Reindex everything: machine_send { machine: \"indexing\", event: \"refresh\", args: { mode: \
\"all\" }, until: [\"choosing_mode\", \"idle\"] }, then start as above.

app: starting -> running -> closing
  starting lasts until the app has finished starting (settings, language packages, the \
startup load queued); context.startup_ms says how long it took. No events from outside: \
watch it. A load asked for meanwhile waits for it (see project).

project: no_project -> loading -> opening -> ready -> unloading -> no_project
  load { path, refresh?: updated|incomplete|all }   no_project/ready: opens a project, as \
project_open does (that uses it). While the app starts, context.waiting_for is \"startup\" \
and the load runs once it has started.
  close                                              ready: closes the project.
  loading ends at ready (no tab is coming: the project has no index yet, or the load changed \
nothing: context.outcome), at opening (the overview tab opens; context.waiting_for \"tab\") or \
at no_project (the load failed). opening ends with the tab's first activation (or after 10 s, \
with a warning in outcome). After an indexing run the new index loads: ready -> loading again.

activation: idle -> activating -> rendering -> idle
  An activation (nav_*, a click in the graph, Back, a search), and anything else that changes \
the graph or code view (expanding a node, a reference in the code view), from its dispatch: \
activating while the app handles it, rendering while the views draw it (context.drawing: \
\"graph\", \"code\"; animations included), idle once they have (context.took_ms). No events \
from outside: watch it. Settled waits for it, and ui_invoke waits for an activation its \
action started (drawn: true). A view that never reports is given up on after 10 s \
(timed_out, with a warning in context.outcome).

question: idle -> asking -> idle
  answer { label } | { option: n } | { id, ... }   asking: answers the question in context \
{ id, text, options, queued } and closes its box. Questions asked meanwhile queue behind it \
(context.queued); answering moves to the next one.

search: idle -> completing -> suggesting -> idle
  The search box's autocompletion. A person typing moves it as an agent's `type` does.
  type { query }                   any state: types query into the box (in place of what is \
there) and asks for its completions: completing, then suggesting (context.suggestions: [{ \
name, type, kind }], the first 30; more: how many are not listed), or idle with \
context.outcome \"nothing matches ...\".
  select { index } | { name }      suggesting: searches for that suggestion, as clicking it \
in the list does: with the filters the box holds (context.tokens, e.g. \"class\"); a previous \
search in the box is replaced. context.outcome names the query that ran. The activation \
follows (watch activation, or ui_state.active_nodes). An agent's type keeps the box's filters \
and completes for what they search for.
  dismiss                          completing/suggesting: closes the list.
  search                           any state: searches for what the box holds, as Enter or \
the search button does: a query starting with ? (case-insensitive) or ?? (case-sensitive) is \
a full-text search, and so is text no suggestion was picked for.
  Recipe: machine_send { machine: \"search\", event: \"type\", args: { query: \"Foo\" }, \
until: [\"suggesting\", \"idle\"] }, then select.

bookmark: idle -> opening -> editing -> idle
  The bookmark editor (Create / Edit Bookmark). A person opening it (the bookmark button, a \
node's context menu, the browser's edit button) moves it to editing too; their typing \
changes context.editor without a move.
  create { node_id? }              idle: opens the editor on what is active (or that node), \
as the bookmark button does; something bookmarked already opens its bookmark (mode edit).
  edit { id }                      idle: opens bookmark id (ui_state.bookmarks).
  opening ends at editing, or at idle with context.outcome (nothing is active, no such \
bookmark, or the editor did not open within 5 s).
  set { name?, comment?, category? }   editing: fills the editor's fields; a category not \
among context.editor.categories is created.
  save                             editing: creates or updates the bookmark and closes the \
editor (refused without a name).
  cancel                           editing: closes it.
  delete { id }                    idle: asks whether to delete bookmark id, as the \
browser's delete button does (see question); context.outcome says when there is no such bookmark.
  context: editor { mode: create|edit, bookmark_id?, node_id?, name, comment, category, \
categories } (editing); outcome (idle).
  Deleting a category, and the bookmark button on a bookmarked symbol, ask a question too \
(see question). A bookmark is activated from the Bookmarks menu (ui_invoke its item).

project_wizard: closed -> applying -> editing -> applying -> closed
  The project wizard (New Project / Edit Project). Every request below moves it to applying \
while the wizard carries it out; wait with until: [\"editing\", \"closed\", \"choosing_type\"]. \
A failed request is back at editing with context.error. A check on the way (a missing path, \
an invalid name) asks a question first (see question): answer it, the request then fails \
with that text. A person's steps move the machine too: opening the wizard, the type \
selection, creating, closing; the context follows their page changes once they leave a page.
  new                              closed: opens the wizard on an empty new project, as Project > \
New Empty Project does.
  new_from_folder { folder, project_location? }   closed: opens it on the project detected \
in folder, as Project > New Project (and the start screen's New Project) does: the name (the folder's), the location \
(project_location, default the folder) and one source group with its fields filled in, shown \
on the general page; nothing is created until create. C/C++, in this order: a cmake group with \
a usable configure preset (a configured one first, then one made for this computer), a cmake \
group on a configured build directory or one beside the project file, a cdb group on a \
compile_commands.json, an empty cpp/c group on the folder (extensions, standard, include \
paths). Ends at editing, or at closed with context.outcome (no such folder).
  edit                             closed: opens it on the project loaded.
  set { project_name?, project_location?, fields?: { ... } }   editing: the general page's \
name and location (a new project's only), and the shown source group's fields: \
context.fields lists them with their values (strings, lists of strings, true/false), e.g. \
source_paths, extensions, exclude_filters, compilation_database, cmake_source_directory, \
cmake_preset, cmake_arguments (without a preset: what the configure adds to cmake -S -B, one \
argument each, e.g. [\"-DBUILD_TESTS=OFF\"]), cmake_index_linked_targets (true: also index \
the targets the filtered ones link to, transitively), header_search_paths, compiler_flags, \
cpp_standard, custom_command.
  show { source_group? }           editing: that source group's page, by its index in \
context.source_groups (0 is the first), or the general one without it.
  add_source_group { type? }       editing: adds a source group of type cdb | cmake | cpp | c \
| rust | swift | custom and shows it; without a type the type selection opens \
(choosing_type, context.types).
  select_type { type }             choosing_type: picks the type there.
  remove_source_group { source_group? }   editing: removes it (default: the one shown).
  duplicate_source_group { source_group? }   editing: adds a copy (\"<name> - Copy\") right \
after it and shows the copy (default: the one shown).
  create                           editing: saves the project (create or save) and loads it: \
closed, context.outcome; the project machine takes over.
  cancel                           editing: closes the wizard; choosing_type: closes the type \
selection.
  context: mode (new | edit), page (general | source_group), source_group, project { name, \
location }, source_groups [{ name, type, enabled }], fields, choices, notes, types \
(choosing_type), applying, error; outcome (closed). choices { field: [{ value, label? }] } \
are the values the page offers for a field (a cmake group: its presets; once its build \
directory is configured, its configurations and targets, for cmake_target_glob: names or \
patterns joined by ';'); notes { field: text } what the page shows beside a field (the build \
directory a preset resolves to, how many targets and files are indexed and which of the \
filter's patterns match no target, \"nosuch*: no target.\"; cmake_error: the \
first error of the build directory's last configure that failed, and when directories it \
adds are missing in a git checkout, the git submodule command). detected { folder, \
source_group_type, reasons, notices? } (a project from a folder): why it is as proposed, the \
first reason the summary; notices what was found besides and decides nothing (the git \
repository and branch, its submodules and which are not checked out, build systems not read \
yet such as Meson or Makefiles).
  Recipe, a project from a folder: new_from_folder { folder: \"C:/src/fmt\", \
project_location: \"C:/projects/fmt\" }; read context.detected and the group's fields (show \
{ source_group: 0 }), set what should differ; create; then index (indexing start).
  Recipe, a project from a compilation database: new; set { project_name, project_location }; \
add_source_group { type: \"cdb\" }; set { fields: { compilation_database: \
\"C:/b/compile_commands.json\" } }; create.

preferences: closed -> applying -> editing -> applying -> closed
  The Preferences dialog (Edit > Preferences). Every request below moves it to applying while \
the dialog carries it out; wait with until: [\"editing\", \"closed\"]. A failed request is back \
at editing with context.error. Save's checks may ask a question first (see question). A \
person's steps move the machine too: opening the dialog, saving, closing it.
  open                             closed: opens the dialog.
  set { fields: { ... } }          editing: fills fields as a person does (a colour scheme \
previews at once): context.fields lists them with their values. Check boxes are true/false \
(animations, builtin_types, hide_deprecated, follow_system_appearance, graph_zoom, logging, \
indexer_logging, ...); lists take one of their items' text (font_size \"14\", color_scheme \
\"dark\", indexer_threads \"default\" or \"4\", auto_scaling \"system\"|\"off\"|\"on\", \
scale_factor \"125%\", font_face a monospaced font); text fields are strings (scroll_speed, \
sourcetrail_port, plugin_port, log_directory, editor_command: Open in Editor's command with \
{file}, {line}, {column}, empty for the detected editor); header_search_paths and framework_search_paths \
are lists of paths. An unknown field, a value of the wrong kind or an item a list lacks is \
refused, naming what there is.
  save                             editing: Save: checks the fields (a bad port, scroll speed, \
log directory: back at editing with context.error), saves them and closes: closed, \
context.outcome \"saved\". Some changes then ask to restart (a question with OK).
  cancel                           editing: closes the dialog without saving (a previewed \
colour scheme is reverted).
  context: fields, applying, error (editing); outcome (closed).
  Recipe: open; set { fields: { font_size: \"16\", animations: false } }; save.

docks: idle -> applying -> idle
  The main window's docks (Search, Graph, Code, Status) and how they are arranged, by the \
docks' own calls, as their buttons and drags do: what a person sees and the layout saved on \
exit follow. Every request below moves it to applying while the window carries it out; wait \
with until: [\"idle\"]: context.outcome says what it did, or context.error how it fell short. A \
request the layout rules out is refused at once, saying why (an unknown dock, listing the \
docks; closing the last open one; tabbing a dock into itself...). A person's docking (a \
drag, a gutter, a dock's buttons, the View menu) is a changed move, source ui, once it \
settled; so is an agent's ui_input drag (describe('input')).
  show { dock }                    a closed dock opens where it was; an auto-hidden one shows \
over the others (overlay); a tab behind another becomes the current one.
  close { dock }                   closes it, as its close button does.
  float { dock }                   into a window of its own, as its float button does.
  dock { dock, to, of? }           docks it at side `to` (left | right | top | bottom) of the \
dock `of`, or of the whole layout without one (a floating or auto-hidden dock too).
  tab { dock, into }               into `into`'s tabs (a docked or a floating dock's).
  select_tab { dock }              makes its tab the current one.
  auto_hide { dock }               into the side bar at the window's nearest edge, as its \
auto-hide button does; show shows it over the others, restore puts it back.
  restore { dock }                 an auto-hidden dock back into the layout, a floating one \
back where it was docked.
  resize { dock, width?, height? } its card to that size, moving the gutters around it (a \
floating dock: its window); context.outcome says the size it got, which the docks around \
it and their minimum sizes may limit.
  reset                            View > Reset Layout: Search across the top, Graph and \
Code side by side, Status across the bottom.
  context: docks [{ name, open, visible, floating, auto_hidden? (the side bar's side), \
overlay?, location? (docked: left | right | top | bottom | center, the side of the layout it \
is at), x?, y? (docked: its card's top-left in the layout), width?, height? (its card, a \
floating one's window), tabs? (the docks in its card, itself included, in order), \
current_tab? }]; applying; outcome or error (what the last request did).
  Recipe: the code view right of the graph, the status tabbed with it: machine_send { \
machine: \"docks\", event: \"dock\", args: { dock: \"Code\", to: \"right\", of: \"Graph\" }, until: \
[\"idle\"] }, then event \"tab\", args { dock: \"Status\", into: \"Code\" }.",
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
    ("debugger", "debug"),
    ("crash", "debug"),
    ("navigation", "nav"),
    ("log", "logs"),
    ("machine", "machines"),
    ("state machine", "machines"),
    ("indexing", "machines"),
    ("question", "machines"),
    ("autocomplete", "machines"),
    ("bookmark", "machines"),
    ("bookmarks", "machines"),
    ("wizard", "machines"),
    ("project_wizard", "machines"),
    ("preferences", "machines"),
    ("settings", "machines"),
    ("docks", "machines"),
    ("dock", "machines"),
    ("layout", "machines"),
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
    fn the_docks_machine_is_documented_with_its_events() {
        let machines = topic("docks").expect("describe('docks') finds a topic");
        assert_eq!(machines.name, "machines");
        for event in [
            "show { dock }",
            "close { dock }",
            "float { dock }",
            "dock { dock, to, of? }",
            "tab { dock, into }",
            "select_tab { dock }",
            "auto_hide { dock }",
            "restore { dock }",
            "resize { dock, width?, height? }",
            "reset",
        ] {
            assert!(machines.doc.contains(event), "the docks machine's {event} is not documented");
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
