//! `sourcetrail-mcp` — the MCP server. Exposes the app's control channels as MCP
//! tools over stdio, and manages app lifecycle + multiple instances (for
//! comparison tests). Requires the `mcp` feature.
//!
//! NOTE: the rmcp surface targets rmcp 3.5 (what Cargo.toml asks for and what
//! this file is written against). Design: context/DESIGN_AGENT_MCP_BRIDGE.md.
//!
//! The SDK carries the protocol, and since 3.x it carries more than one
//! revision of it: a client that opens with `initialize` negotiates a session
//! as before, and one that speaks 2026-07-28 sends no handshake at all -- it
//! opens with `server/discover` or any request carrying the required `_meta`,
//! and rmcp serves it without a session. Nothing here chooses between them; the
//! client does, per connection.

use std::path::PathBuf;
use std::time::Duration;
use std::sync::mpsc;

use agent_mcp_bridge::catalog;
use agent_mcp_bridge::instance::{InstanceManager, StartOpts};
use agent_mcp_bridge::protocol::{self, InputEvent, InputKind, InputTarget};
use serde_json::{json, Value};
use tokio::sync::oneshot;

use rmcp::{
    handler::server::wrapper::Parameters,
    model::{
        CallToolResult, ContentBlock, Implementation, ProtocolVersion, ServerCapabilities,
        ServerConfig,
    },
    tool, tool_handler, tool_router,
    transport::stdio,
    ErrorData as McpError, ServerHandler, ServiceExt,
};

// --- Manager actor ----------------------------------------------------------
// InstanceManager owns thoth-ipc Routes + child processes (not Sync), so it lives
// on one dedicated OS thread and serves closures from the async rmcp handlers.

type Job = Box<dyn FnOnce(&mut InstanceManager) -> anyhow::Result<Value> + Send>;

#[derive(Clone)]
struct ManagerHandle {
    tx: mpsc::Sender<(Job, oneshot::Sender<anyhow::Result<Value>>)>,
}

impl ManagerHandle {
    fn spawn() -> Self {
        let (tx, rx) = mpsc::channel::<(Job, oneshot::Sender<anyhow::Result<Value>>)>();
        std::thread::spawn(move || {
            let mut mgr = InstanceManager::new();
            while let Ok((job, reply)) = rx.recv() {
                let _ = reply.send(job(&mut mgr));
            }
            // rx closed -> mgr drops -> spawned children are killed.
        });
        Self { tx }
    }

    async fn call<F>(&self, f: F) -> Result<Value, McpError>
    where
        F: FnOnce(&mut InstanceManager) -> anyhow::Result<Value> + Send + 'static,
    {
        let (rtx, rrx) = oneshot::channel();
        self.tx
            .send((Box::new(f), rtx))
            .map_err(|_| McpError::internal_error("manager thread gone", None))?;
        // `{:#}`: the whole context chain ("attach to instance 'x': <why>"), not
        // just the outermost context.
        let mut message = match rrx.await.map_err(|_| McpError::internal_error("manager dropped reply", None))? {
            Ok(v) => return Ok(v),
            Err(e) => format!("{e:#}"),
        };
        // An app that stopped in its debugger during the call (a crash, a
        // breakpoint) answers nothing: say so rather than only "timed out".
        if message.contains("timed out") {
            if let Ok(Value::String(stops)) = self.call_raw(|m| Ok(json!(m.debugger_stops()))).await {
                if !stops.is_empty() {
                    message.push_str(&format!(". Stopped in the debugger: {stops} (instance_debug_command)"));
                }
            }
        }
        Err(McpError::internal_error(message, None))
    }

    /// A job without the error formatting of [`Self::call`].
    async fn call_raw<F>(&self, f: F) -> anyhow::Result<Value>
    where
        F: FnOnce(&mut InstanceManager) -> anyhow::Result<Value> + Send + 'static,
    {
        let (rtx, rrx) = oneshot::channel();
        self.tx.send((Box::new(f), rtx)).map_err(|_| anyhow::anyhow!("manager thread gone"))?;
        rrx.await.map_err(|_| anyhow::anyhow!("manager dropped reply"))?
    }
}

fn ok_json(v: Value) -> Result<CallToolResult, McpError> {
    Ok(CallToolResult::success(vec![ContentBlock::text(
        serde_json::to_string_pretty(&v).unwrap_or_else(|_| v.to_string()),
    )]))
}

/// Turn a `{ ok, frame: { ..., image_base64 } }` capture into what a client can
/// use: the PNG as an MCP image block (a model sees images, not base64 text),
/// plus the metadata as JSON without the payload. `{ ok: false, message }`
/// passes through as JSON.
fn frame_result(mut v: Value) -> Result<CallToolResult, McpError> {
    let Some(b64) = v["frame"]["image_base64"].as_str().map(str::to_owned) else {
        return ok_json(v);
    };
    if let Some(frame) = v["frame"].as_object_mut() {
        frame.remove("image_base64");
    }
    let meta = ContentBlock::text(serde_json::to_string_pretty(&v).unwrap_or_else(|_| v.to_string()));
    Ok(CallToolResult::success(vec![ContentBlock::image(b64, "image/png"), meta]))
}

// --- Tool argument schemas --------------------------------------------------
// Every driving tool takes an optional `instance` (default = the one running
// instance, else the unnamespaced app); the manager lazily attaches to it on
// first use. Every schema denies unknown fields, nested ones included: a
// misspelled argument fails naming the field, instead of being dropped so the
// call goes ahead on the default.

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct InstanceArg {
    #[serde(default)]
    instance: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct RefPathStep {
    role: String,
    #[serde(default)]
    name: String,
    #[serde(default)]
    index: u32,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct ElementRefArg {
    #[serde(default)]
    object_name: String,
    #[serde(default)]
    path: Vec<RefPathStep>,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct InvokeActionArgs {
    /// The element to act on — pass a `ref` object from ui_snapshot (object_name + path).
    #[serde(rename = "ref")]
    target: ElementRefArg,
    /// Accessible action name from the node's `actions` (e.g. "Press", "Toggle", "ShowMenu").
    #[serde(default)]
    action: String,
    /// Optional: set the element's editable text/value (line edits, combos).
    #[serde(default)]
    text: String,
    /// Optional optimistic-control guard: pass the snapshot node's `hash`; the
    /// action is dropped if the element changed since (0 = no check).
    #[serde(default)]
    expect_hash: u64,
    #[serde(default)]
    instance: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct CaptureElementArgs {
    /// The element to screenshot — pass a `ref` from ui_snapshot.
    #[serde(rename = "ref")]
    target: ElementRefArg,
    /// Also return the element's properties (name/value/actions). (Reserved.)
    #[serde(default)]
    include_properties: bool,
    /// Optional optimistic-control guard (snapshot node `hash`); 0 = no check.
    #[serde(default)]
    expect_hash: u64,
    #[serde(default)]
    instance: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct SetLogFilterArgs {
    /// Qt category filter rules (QLoggingCategory::setFilterRules), e.g.
    /// "qt.qpa.*=false\nmyapp.net.debug=true". Empty leaves Qt emission as-is.
    #[serde(default)]
    qt_rules: String,
    /// Regex matched against "category: message"; only matches raise a LogEvent
    /// (empty = forward all at/above min_level).
    #[serde(default)]
    event_pattern: String,
    /// Forward floor: "info" | "warning" | "error".
    #[serde(default = "default_warning")]
    min_level: String,
    #[serde(default)]
    instance: String,
}

fn default_warning() -> String {
    "warning".to_string()
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct PollEventsArgs {
    /// Cursor: return events with seq greater than this. Start at 0; pass the
    /// returned `latest_seq` next time.
    #[serde(default)]
    since_seq: u64,
    #[serde(default)]
    instance: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct QueryArgs {
    query: String,
    #[serde(default)]
    instance: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct ActivateNodeArgs {
    #[serde(default)]
    serialized_name: Option<String>,
    #[serde(default)]
    node_id: u64,
    #[serde(default)]
    instance: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct PathArgs {
    path: String,
    #[serde(default)]
    instance: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct TabArgs {
    /// Tab id (from ui_state.tabs[].tab_id).
    tab_id: u64,
    #[serde(default)]
    instance: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct SnapshotArgs {
    /// Raw object tree (widgets + Q_PROPERTY bag) instead of the default accessibility tree.
    #[serde(default)]
    object_tree: bool,
    #[serde(default)]
    instance: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct StartArgs {
    /// Path to the Sourcetrail binary to launch.
    bin: String,
    /// Explicit instance id; if omitted, derived from the binary's git checkout
    /// (`<branch>-<shorthash>`).
    #[serde(default)]
    id: String,
    /// Project to load once the app is up.
    #[serde(default)]
    project: Option<String>,
    /// Run offscreen (`QT_QPA_PLATFORM=offscreen`); default true.
    #[serde(default = "default_true")]
    headless: bool,
    /// Settings profile: a name for an isolated, persistent user-data directory
    /// (default "agent"), or "user" for your own settings.
    #[serde(default)]
    profile: String,
    /// Run on a throwaway copy of the profile (deleted when the instance stops).
    #[serde(default)]
    fresh: bool,
    /// Attach the platform debugger (crash dumps, live debugging); default: when
    /// one is installed (cdb on Windows, lldb on macOS).
    #[serde(default)]
    debugger: Option<bool>,
    /// Keep the app's animations (graph transitions, smooth scrolling). Off by
    /// default: settling waits for them, so each navigation takes longer.
    #[serde(default)]
    animations: bool,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct DebugArgs {
    /// The instance to debug; empty = the one running instance.
    #[serde(default)]
    instance: String,
    /// For an app the bridge attached to rather than started: its process id.
    #[serde(default)]
    pid: Option<u32>,
    /// Stop the app right away and return the stop.
    #[serde(default)]
    break_in: bool,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct DebugCommandArgs {
    /// The instance (or open dump) whose debugger runs the command.
    #[serde(default)]
    instance: String,
    /// A command in the debugger's own language (cdb: `~*k`, `.ecxr; k`,
    /// `!analyze -v`, `g`; lldb: `bt all`, `process continue`).
    command: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct DebugDumpArgs {
    /// The dump to open (`.dmp` on Windows, a core on macOS).
    path: String,
    /// The id to address it by; default `dump-<file stem>`.
    #[serde(default)]
    id: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct KillArgs {
    /// The instance to stop; empty = the one running instance.
    #[serde(default)]
    instance: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct ScrollArgs {
    /// Source file whose line to show.
    path: String,
    /// 1-based line number.
    line: u32,
    #[serde(default)]
    instance: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct HistoryOfArgs {
    /// One request (a request_id from events_poll); default: the last ones.
    #[serde(default)]
    request_id: Option<u64>,
    /// How many of the last requests, without request_id. Default 10.
    #[serde(default = "default_history_last")]
    last: usize,
    #[serde(default)]
    instance: String,
}

fn default_history_last() -> usize {
    10
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct GraphNodeArgs {
    /// A node of the graph: ui_state.view_graph[].id.
    node_id: u64,
    #[serde(default)]
    instance: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct ExpandArgs {
    /// A node with an expand toggle: ui_state.view_graph[].id where `expanded` is present.
    node_id: u64,
    /// false collapses.
    #[serde(default = "default_true")]
    expand: bool,
    #[serde(default)]
    instance: String,
}

fn default_true() -> bool {
    true
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct SplitArgs {
    /// A bundle or group: ui_state.view_graph[].id where `splittable` is true.
    bundle_id: u64,
    #[serde(default)]
    instance: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct TrailArgs {
    /// The node the trail starts from (or ends at, with direction "incoming").
    node_id: u64,
    /// "outgoing" (what a function calls, a class's derived classes, what a file
    /// includes) or "incoming" (callers, base classes, includers). Default outgoing.
    #[serde(default)]
    direction: Option<String>,
    /// A trail from node_id to this node instead.
    #[serde(default)]
    target_id: Option<u64>,
    /// Edge kinds by name ("call", "override", "inheritance", "include", "use", ...);
    /// default: by the node's kind, as the graph's trail buttons choose.
    #[serde(default)]
    edge_kinds: Vec<String>,
    /// Node kinds to keep ("class", "function", ...); default: all.
    #[serde(default)]
    node_kinds: Vec<String>,
    /// How many edges deep; 0 (default): as deep as it goes.
    #[serde(default)]
    depth: u32,
    /// "horizontal" or "vertical"; default: vertical for inheritance, else horizontal.
    #[serde(default)]
    layout: Option<String>,
    /// Keep nodes outside the index (unresolved symbols) on the trail.
    #[serde(default)]
    include_non_indexed: bool,
    #[serde(default)]
    instance: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct HistoryArgs {
    /// "back" or "forward".
    direction: String,
    #[serde(default)]
    instance: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct RegionArg {
    x: i32,
    y: i32,
    w: i32,
    h: i32,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct FrameArgs {
    /// Downscale to this width in pixels (aspect kept); 0 = full size.
    #[serde(default)]
    max_width: u32,
    /// Crop to this rectangle, in the full frame's pixels, before downscaling.
    #[serde(default)]
    region: Option<RegionArg>,
    #[serde(default)]
    instance: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct InputEventArg {
    /// move | press | release | doubleclick | wheel | keypress | keyrelease
    kind: String,
    /// Mouse position: relative to the `ref` element, global for `at`, frame pixels
    /// for `at_frame`.
    #[serde(default)]
    x: f64,
    #[serde(default)]
    y: f64,
    /// Put a mouse event at the element's center (`ref`) or at the point itself.
    #[serde(default)]
    centered: bool,
    /// left (default) | right | middle | none, for press/release/doubleclick.
    #[serde(default)]
    button: Option<String>,
    /// Buttons held during a move or wheel (drag): any of left, right, middle.
    #[serde(default)]
    buttons: Vec<String>,
    /// Any of shift, ctrl, alt, meta.
    #[serde(default)]
    modifiers: Vec<String>,
    /// Wheel: 120 = one notch up, -120 one down.
    #[serde(default)]
    wheel_delta: i32,
    /// Key events: a name (Enter, Escape, Tab, Left, F5, ...), one character, or a Qt::Key code.
    #[serde(default)]
    key: String,
    /// Key events: the text the key types (default: the key's character).
    #[serde(default)]
    text: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct InputArgs {
    /// The element to send to: a `ref` from ui_snapshot / ui_query.
    #[serde(default, rename = "ref")]
    target: Option<ElementRefArg>,
    /// Or the widget under this global point [x, y] (ui_snapshot rects are global).
    #[serde(default)]
    at: Option<[i32; 2]>,
    /// Or under this pixel [x, y] of the last ui_frame (for what only the picture
    /// shows, like graph nodes); event x/y are then frame pixels too.
    #[serde(default)]
    at_frame: Option<[f64; 2]>,
    /// A ready-made gesture at the target's center: click | double_click | right_click | hover.
    #[serde(default)]
    gesture: Option<String>,
    /// Or the events themselves, delivered in order (a drag: press, move..., release).
    #[serde(default)]
    events: Vec<InputEventArg>,
    #[serde(default)]
    instance: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct MachineStateArgs {
    /// A machine's name ("indexing", "question"); empty for all of them.
    #[serde(default)]
    machine: String,
    /// Wait until the machine is in one of these states (needs `machine`).
    #[serde(default)]
    until: Vec<String>,
    /// How long to wait for `until`, in seconds (default 60, at most 300).
    #[serde(default)]
    timeout_s: Option<u64>,
    #[serde(default)]
    instance: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct MachineSendArgs {
    /// The machine: "indexing" or "question".
    machine: String,
    /// One of the events the machine accepts now (machine_state lists them).
    event: String,
    /// The event's arguments, a JSON object: {"mode": "all"} for indexing's
    /// select_mode/start, {"label": "Reindex"} or {"option": 0} for question's answer.
    #[serde(default)]
    args: Option<Value>,
    /// Then wait until the machine is in one of these states.
    #[serde(default)]
    until: Vec<String>,
    /// How long to wait for `until`, in seconds (default 60, at most 300).
    #[serde(default)]
    timeout_s: Option<u64>,
    #[serde(default)]
    instance: String,
}

/// The wait for a machine's `until`: 60 s unless asked, at most 300 s (the call
/// holds the bridge meanwhile).
fn machine_timeout(timeout_s: Option<u64>) -> Duration {
    Duration::from_secs(timeout_s.unwrap_or(60).clamp(1, 300))
}

/// The events a ready-made gesture stands for, all at the target's center.
fn gesture_events(gesture: &str) -> Result<Vec<InputEvent>, McpError> {
    let at = |kind, button| InputEvent { kind, centered: true, button, ..Default::default() };
    Ok(match gesture.trim().to_ascii_lowercase().as_str() {
        "click" => vec![at(InputKind::Press, 1), at(InputKind::Release, 1)],
        "double_click" | "doubleclick" => vec![
            at(InputKind::Press, 1),
            at(InputKind::Release, 1),
            at(InputKind::DoubleClick, 1),
            at(InputKind::Release, 1),
        ],
        "right_click" | "rightclick" => vec![at(InputKind::Press, 2), at(InputKind::Release, 2)],
        "hover" => vec![at(InputKind::Move, 0)],
        other => {
            return Err(McpError::invalid_params(
                format!("gesture must be click, double_click, right_click or hover, not \"{other}\""),
                None,
            ))
        }
    })
}

/// The option `value` names (trimmed, in any case); else an error that lists them.
fn one_of<T: Copy>(what: &str, value: &str, options: &[(&str, T)]) -> Result<T, McpError> {
    let lower = value.trim().to_ascii_lowercase();
    options.iter().find(|(name, _)| *name == lower).map(|(_, option)| *option).ok_or_else(|| {
        let names: Vec<&str> = options.iter().map(|(name, _)| *name).collect();
        let listed = match names.split_last() {
            Some((last, rest)) if !rest.is_empty() => format!("{} or {last}", rest.join(", ")),
            _ => names.concat(),
        };
        McpError::invalid_params(format!("{what} must be {listed}, not \"{value}\""), None)
    })
}

fn mouse_button(name: Option<&str>, kind: InputKind) -> Result<u8, McpError> {
    let default = if matches!(kind, InputKind::Press | InputKind::Release | InputKind::DoubleClick) { "left" } else { "none" };
    one_of("button", name.unwrap_or(default), &[("left", 1), ("right", 2), ("middle", 3), ("none", 0)])
}

fn bit_mask(names: &[String], table: &[(&str, u32)], what: &str) -> Result<u32, McpError> {
    names.iter().try_fold(0, |mask, name| {
        let lower = name.trim().to_ascii_lowercase();
        table.iter().find(|(n, _)| *n == lower).map(|(_, bit)| mask | bit).ok_or_else(|| {
            let valid: Vec<&str> = table.iter().map(|(n, _)| *n).collect();
            McpError::invalid_params(format!("{what} \"{name}\" is not one of {}", valid.join(", ")), None)
        })
    })
}

/// The wire form of one `ui_input` event; `point` maps its position (frame pixels
/// to global, or as is).
fn input_event(e: &InputEventArg, point: &dyn Fn(f64, f64) -> anyhow::Result<(i32, i32)>) -> Result<InputEvent, McpError> {
    let kind = InputKind::parse(&e.kind).ok_or_else(|| {
        McpError::invalid_params(
            format!("kind must be move, press, release, doubleclick, wheel, keypress or keyrelease, not \"{}\"", e.kind),
            None,
        )
    })?;
    let (x, y) = if kind.is_mouse() && !e.centered {
        point(e.x, e.y).map_err(|err| McpError::invalid_params(format!("{err:#}"), None))?
    } else {
        (0, 0)
    };
    let (key, text) = if kind.is_mouse() {
        (0, String::new())
    } else {
        let key = protocol::qt_key(&e.key)
            .ok_or_else(|| McpError::invalid_params(format!("unknown key \"{}\"", e.key), None))?;
        let text = if !e.text.is_empty() || e.key.chars().count() != 1 { e.text.clone() } else { e.key.clone() };
        (key, text)
    };
    Ok(InputEvent {
        kind,
        x,
        y,
        centered: e.centered,
        button: mouse_button(e.button.as_deref(), kind)?,
        buttons: bit_mask(&e.buttons, &[("left", 1), ("right", 2), ("middle", 4)], "button")?,
        modifiers: bit_mask(&e.modifiers, &[("shift", 1), ("ctrl", 2), ("alt", 4), ("meta", 8)], "modifier")?,
        wheel_delta: e.wheel_delta,
        key,
        text,
    })
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
#[serde(deny_unknown_fields)]
struct DescribeArgs {
    /// Topic (or tool name); empty lists the topics.
    #[serde(default)]
    topic: String,
}

// --- MCP server -------------------------------------------------------------

// No `tool_router` field: `#[tool_handler]` calls `Self::tool_router()` itself,
// so a stored copy is one the server never reads.
//
// Tool names carry an area prefix (instance_, ui_, nav_, graph_, events_, logs_,
// project_), so a client that searches or defers tool definitions finds a whole
// area with one query. Descriptions stay one line with the result shape; the
// detail is in catalog.rs, served by `describe`.
#[derive(Clone)]
struct SourcetrailServer {
    mgr: ManagerHandle,
}

fn element_path(target: &ElementRefArg) -> Vec<(String, String, u32)> {
    target.path.iter().map(|s| (s.role.clone(), s.name.clone(), s.index)).collect()
}

#[tool_router]
impl SourcetrailServer {
    fn new(mgr: ManagerHandle) -> Self {
        Self { mgr }
    }

    #[tool(description = "Start here. With no topic, lists the topics; with one (e.g. describe('nav')) or a tool name, returns that area's usage notes and exact result shapes. Returns { topics } or { topic, tools, doc }.")]
    async fn describe(&self, Parameters(a): Parameters<DescribeArgs>) -> Result<CallToolResult, McpError> {
        ok_json(catalog::describe(&a.topic))
    }

    // --- instances ---

    #[tool(description = "Launch a Sourcetrail process with agent control (headless, on an isolated settings profile by default). Needs `bin`. Returns { id } for the `instance` argument; describe('instances').")]
    async fn instance_start(&self, Parameters(a): Parameters<StartArgs>) -> Result<CallToolResult, McpError> {
        ok_json(
            self.mgr
                .call(move |m| {
                    let id = m.start(StartOpts {
                        bin: PathBuf::from(a.bin),
                        id: a.id,
                        project: a.project,
                        headless: a.headless,
                        profile: a.profile,
                        fresh: a.fresh,
                        debugger: a.debugger,
                        animations: a.animations,
                    })?;
                    Ok(json!({ "id": id }))
                })
                .await?,
        )
    }

    #[tool(description = "Stop a spawned instance (or forget an attached one). Empty instance = the one running instance. Returns { killed: id }.")]
    async fn instance_stop(&self, Parameters(a): Parameters<KillArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| Ok(json!({ "killed": m.kill(&a.instance)? }))).await?)
    }

    #[tool(description = "List known instances and open dumps. Returns { instances: [ { id, pid, running, exit_code?, stopped?, dump?, console_file?, ... } | { id, start_failed } ] }.")]
    async fn instance_list(&self) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(|m| Ok(m.list())).await?)
    }

    #[tool(description = "Check an instance's protocol version against the bridge's. Returns { ok, bridge_protocol_version, app_protocol_version, app_version, skew: match|app_older|app_newer, wire }.")]
    async fn instance_handshake(&self, Parameters(a): Parameters<InstanceArg>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| Ok(m.get_or_attach(&a.instance)?.bridge().handshake())).await?)
    }

    #[tool(description = "Attach the platform debugger to an instance (instance_start already does) or stop it with break_in. Returns { debugger, dump, dump_written, stopped, connect }; describe('debug').")]
    async fn instance_debug(&self, Parameters(a): Parameters<DebugArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| m.debug(&a.instance, a.pid, a.break_in)).await?)
    }

    #[tool(description = "Run a debugger command (cdb: ~*k, .ecxr; k, !analyze -v, g; lldb: bt all) on an instance or open dump; a running app is stopped for it and resumed. Returns { output, stopped }.")]
    async fn instance_debug_command(&self, Parameters(a): Parameters<DebugCommandArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| m.debug_command(&a.instance, &a.command)).await?)
    }

    #[tool(description = "Open a crash dump post-mortem in the platform debugger, as an id for instance_debug_command; instance_stop closes it. Returns { id, debugger, dump, stopped }.")]
    async fn instance_debug_dump(&self, Parameters(a): Parameters<DebugDumpArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| m.open_dump(std::path::Path::new(&a.path), &a.id)).await?)
    }

    // --- state + events ---

    #[tool(description = "Read the app's state: project, app_state (NoProject/Loading/Indexing/Ready/Busy), active nodes, graph, code view, search matches, tabs. Returns the ui_state object; describe('state').")]
    async fn ui_state(&self, Parameters(a): Parameters<InstanceArg>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| m.get_or_attach(&a.instance)?.bridge().get_ui_state()).await?)
    }

    #[tool(description = "Await transitions (app_state, indexing, activations) instead of re-polling ui_state. Returns { events: [ { seq, type, ... } ], latest_seq }; pass latest_seq back as since_seq.")]
    async fn events_poll(&self, Parameters(a): Parameters<PollEventsArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| Ok(m.get_or_attach(&a.instance)?.bridge().poll_events(a.since_seq))).await?)
    }

    #[tool(description = "Forward the app's log lines into events_poll as LogEvents, filtered by min_level, a regex and Qt category rules. Returns { ok, message }; describe('logs').")]
    async fn logs_forward(&self, Parameters(a): Parameters<SetLogFilterArgs>) -> Result<CallToolResult, McpError> {
        ok_json(
            self.mgr
                .call(move |m| {
                    m.get_or_attach(&a.instance)?.bridge().set_log_filter(&a.qt_rules, &a.event_pattern, &a.min_level)
                })
                .await?,
        )
    }

    // --- project ---

    #[tool(description = "Open a Sourcetrail project (.srctrlprj / .srctrl.toml) and wait until it is usable. Returns the ui_state once loading is done (Ready, or the indexing dialog in `dialog`).")]
    async fn project_open(&self, Parameters(a): Parameters<PathArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| m.get_or_attach(&a.instance)?.bridge().load_project(&a.path)).await?)
    }

    // --- navigation ---

    #[tool(description = "Search the indexed code base (unranked). Returns [ { text, node_kind, node_ids, score } ]. Use nav_find to pick one element.")]
    async fn nav_search(&self, Parameters(a): Parameters<QueryArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| m.get_or_attach(&a.instance)?.bridge().search(&a.query)).await?)
    }

    #[tool(description = "Resolve a name to the best-matching code elements, ranked (exact, prefix, substring, score). Returns { query, matches }; activate matches[0].node_ids[0].")]
    async fn nav_find(&self, Parameters(a): Parameters<QueryArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| m.get_or_attach(&a.instance)?.bridge().find_element(&a.query)).await?)
    }

    #[tool(description = "Show a code element in the graph and code view, by node_id or serialized_name. Returns the settled ui_state.")]
    async fn nav_activate_node(&self, Parameters(a): Parameters<ActivateNodeArgs>) -> Result<CallToolResult, McpError> {
        ok_json(
            self.mgr
                .call(move |m| m.get_or_attach(&a.instance)?.bridge().activate_node(a.serialized_name.as_deref(), a.node_id))
                .await?,
        )
    }

    #[tool(description = "Open a source file in the code view. Returns the settled ui_state.")]
    async fn nav_activate_file(&self, Parameters(a): Parameters<PathArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| m.get_or_attach(&a.instance)?.bridge().activate_file(&a.path)).await?)
    }

    #[tool(description = "Switch to a tab by tab_id (from ui_state.tabs). Returns the settled ui_state.")]
    async fn nav_activate_tab(&self, Parameters(a): Parameters<TabArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| m.get_or_attach(&a.instance)?.bridge().activate_tab(a.tab_id)).await?)
    }

    #[tool(description = "Scroll the code view to a 1-based line of a source file. Returns the settled ui_state.")]
    async fn nav_scroll_to_line(&self, Parameters(a): Parameters<ScrollArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| m.get_or_attach(&a.instance)?.bridge().scroll_to_line(&a.path, a.line)).await?)
    }

    #[tool(description = "What the commands sent led to: per request its command, ack, settle time and the events caused_by it (activations, machine transitions, dialogs); reads like ui_state are left out. Returns { requests: [...] }.")]
    async fn events_history(&self, Parameters(a): Parameters<HistoryOfArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| Ok(m.get_or_attach(&a.instance)?.bridge().request_log(a.request_id, a.last))).await?)
    }

    #[tool(description = "Expand (or with expand=false collapse) a graph node's members, like its expand toggle. node_id from ui_state.view_graph. Returns the settled ui_state.")]
    async fn graph_expand(&self, Parameters(a): Parameters<ExpandArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| m.get_or_attach(&a.instance)?.bridge().expand_node(a.node_id, a.expand)).await?)
    }

    #[tool(description = "Hide a node (or an edge, by its id) in the graph, like its context menu's Hide. Returns the settled ui_state.")]
    async fn graph_hide(&self, Parameters(a): Parameters<GraphNodeArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| m.get_or_attach(&a.instance)?.bridge().hide_node(a.node_id)).await?)
    }

    #[tool(description = "Split a bundle or group of the graph into its nodes, like clicking it. bundle_id from ui_state.view_graph (splittable). Returns the settled ui_state.")]
    async fn graph_split(&self, Parameters(a): Parameters<SplitArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| m.get_or_attach(&a.instance)?.bridge().split_bundle(a.bundle_id)).await?)
    }

    #[tool(description = "Show a trail: a function's callees or callers, a class's derived or base classes, a file's includes or includers; or the paths to target_id. Returns the settled ui_state.")]
    async fn graph_trail(&self, Parameters(a): Parameters<TrailArgs>) -> Result<CallToolResult, McpError> {
        let incoming = a
            .direction
            .as_deref()
            .map(|d| one_of("direction", d, &[("outgoing", false), ("incoming", true)]))
            .transpose()?
            .unwrap_or(false);
        let horizontal =
            a.layout.as_deref().map(|l| one_of("layout", l, &[("horizontal", true), ("vertical", false)])).transpose()?;
        ok_json(
            self.mgr
                .call(move |m| {
                    m.get_or_attach(&a.instance)?.bridge().activate_trail(&protocol::Trail {
                        node_id: a.node_id,
                        incoming,
                        target_id: a.target_id.unwrap_or(0),
                        edge_kinds: &a.edge_kinds,
                        node_kinds: &a.node_kinds,
                        depth: a.depth,
                        horizontal,
                        include_non_indexed: a.include_non_indexed,
                    })
                })
                .await?,
        )
    }

    #[tool(description = "Navigate back or forward in the activation history, like the toolbar arrows. direction: back|forward. Returns the settled ui_state.")]
    async fn nav_history(&self, Parameters(a): Parameters<HistoryArgs>) -> Result<CallToolResult, McpError> {
        let forward = one_of("direction", &a.direction, &[("back", false), ("forward", true)])?;
        ok_json(self.mgr.call(move |m| m.get_or_attach(&a.instance)?.bridge().history(forward)).await?)
    }

    // --- structural UI ---

    #[tool(description = "Capture the widget tree: each element's role, name, rect, actions and a `ref` to target. Accessibility tree by default, object_tree=true for widgets. Returns { tree_hash, roots }; describe('ui').")]
    async fn ui_snapshot(&self, Parameters(a): Parameters<SnapshotArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| m.get_or_attach(&a.instance)?.bridge().get_snapshot(a.object_tree)).await?)
    }

    #[tool(description = "Select UI elements by JSONPath (RFC 9535) over the accessibility tree, in the app. Returns the ui_snapshot shape with the matches as roots, e.g. $.roots..[?(@.role=='button')].")]
    async fn ui_query(&self, Parameters(a): Parameters<QueryArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| m.get_or_attach(&a.instance)?.bridge().query_ui(&a.query)).await?)
    }

    #[tool(description = "Run an accessible action (Press, Toggle, ShowMenu...) or set `text` on the element a ui_snapshot `ref` names. Returns { ok, message }; not found / unsupported is ok: false.")]
    async fn ui_invoke(&self, Parameters(a): Parameters<InvokeActionArgs>) -> Result<CallToolResult, McpError> {
        ok_json(
            self.mgr
                .call(move |m| {
                    let path = element_path(&a.target);
                    m.get_or_attach(&a.instance)?.bridge().invoke_action(
                        &a.target.object_name,
                        &path,
                        &a.action,
                        &a.text,
                        a.expect_hash,
                    )
                })
                .await?,
        )
    }

    #[tool(description = "See the screen as a user would: every visible window, dialogs included, composited. Returns a PNG image plus { ok, frame: { width, height }, windows }; region crops, max_width downscales.")]
    async fn ui_frame(&self, Parameters(a): Parameters<FrameArgs>) -> Result<CallToolResult, McpError> {
        let region = a.region.map(|r| [r.x, r.y, r.w, r.h]);
        if region.is_some_and(|r| r[2] <= 0 || r[3] <= 0) {
            return Err(McpError::invalid_params("region needs a positive w and h", None));
        }
        frame_result(
            self.mgr.call(move |m| m.get_or_attach(&a.instance)?.bridge().get_frame(a.max_width, region)).await?,
        )
    }

    #[tool(description = "Mouse and keyboard on a `ref`, a global point `at`, or a pixel of the last ui_frame `at_frame` (graph nodes): a `gesture` or `events`. Returns { ok, message, settled, state }; describe('input').")]
    async fn ui_input(&self, Parameters(a): Parameters<InputArgs>) -> Result<CallToolResult, McpError> {
        let targets = usize::from(a.target.is_some()) + usize::from(a.at.is_some()) + usize::from(a.at_frame.is_some());
        if targets != 1 {
            return Err(McpError::invalid_params("give exactly one of ref, at, at_frame", None));
        }
        if a.gesture.is_some() == !a.events.is_empty() {
            return Err(McpError::invalid_params("give either a gesture or events", None));
        }
        let gesture = a.gesture.as_deref().map(gesture_events).transpose()?;
        ok_json(
            self.mgr
                .call(move |m| {
                    let bridge = m.get_or_attach(&a.instance)?.bridge();
                    // Positions: frame pixels for at_frame, else as given.
                    let to_global = |x: f64, y: f64| -> anyhow::Result<(i32, i32)> {
                        if a.at_frame.is_some() {
                            bridge.frame_to_global(x, y)
                        } else {
                            Ok((x.round() as i32, y.round() as i32))
                        }
                    };
                    let events = match gesture {
                        Some(events) => events,
                        None => a
                            .events
                            .iter()
                            .map(|e| input_event(e, &to_global))
                            .collect::<Result<Vec<_>, _>>()
                            .map_err(|e| anyhow::anyhow!(e.message.to_string()))?,
                    };
                    let path = a.target.as_ref().map(element_path).unwrap_or_default();
                    let object_name = a.target.as_ref().map(|t| t.object_name.clone()).unwrap_or_default();
                    let target = match (a.at, a.at_frame) {
                        (Some([x, y]), _) => InputTarget::Point { x, y },
                        (None, Some([px, py])) => {
                            let (x, y) = bridge.frame_to_global(px, py)?;
                            InputTarget::Point { x, y }
                        }
                        (None, None) => InputTarget::Ref { object_name: &object_name, path: &path },
                    };
                    bridge.send_input(&target, &events)
                })
                .await?,
        )
    }

    #[tool(description = "The state machines (indexing, question): state, events accepted now, context. `until` waits for a state. Returns { machines: [...] } or { machine, transitions, reached }; describe('machines').")]
    async fn machine_state(&self, Parameters(a): Parameters<MachineStateArgs>) -> Result<CallToolResult, McpError> {
        if !a.until.is_empty() && a.machine.is_empty() {
            return Err(McpError::invalid_params("`until` needs a `machine`", None));
        }
        let timeout = machine_timeout(a.timeout_s);
        ok_json(
            self.mgr
                .call(move |m| {
                    let bridge = m.get_or_attach(&a.instance)?.bridge();
                    if a.until.is_empty() {
                        bridge.get_machines(&a.machine)
                    } else {
                        bridge.wait_machine(&a.machine, &a.until, timeout)
                    }
                })
                .await?,
        )
    }

    #[tool(description = "Send a state machine an event, as its dialog's button would (start indexing, answer a question). Returns { ok, message, machine, transitions }, + reached with `until`; describe('machines').")]
    async fn machine_send(&self, Parameters(a): Parameters<MachineSendArgs>) -> Result<CallToolResult, McpError> {
        let args = match &a.args {
            None => String::new(),
            Some(v @ Value::Object(_)) => v.to_string(),
            Some(_) => return Err(McpError::invalid_params("`args` must be a JSON object", None)),
        };
        let timeout = machine_timeout(a.timeout_s);
        ok_json(
            self.mgr
                .call(move |m| {
                    m.get_or_attach(&a.instance)?.bridge().send_machine_event(
                        &a.machine,
                        &a.event,
                        &args,
                        &a.until,
                        timeout,
                    )
                })
                .await?,
        )
    }

    #[tool(description = "Screenshot one element by its ui_snapshot `ref`. Returns a PNG image plus { ok, frame: { width, height } }, or { ok: false, message }.")]
    async fn ui_capture(&self, Parameters(a): Parameters<CaptureElementArgs>) -> Result<CallToolResult, McpError> {
        frame_result(
            self.mgr
                .call(move |m| {
                    let path = element_path(&a.target);
                    m.get_or_attach(&a.instance)?.bridge().capture_element(
                        &a.target.object_name,
                        &path,
                        a.include_properties,
                        a.expect_hash,
                    )
                })
                .await?,
        )
    }
}

#[tool_handler]
impl ServerHandler for SourcetrailServer {
    fn get_info(&self) -> ServerConfig {
        // Built with the SDK's own builders rather than a struct literal: these
        // types are `#[non_exhaustive]`, so a revision that adds a field is a
        // field this file never has to learn about.
        ServerConfig::new(ServerCapabilities::builder().enable_tools().build())
            // The newest revision the SDK implements. Older clients are still
            // answered in their own: rmcp negotiates per connection, so one
            // binary serves an editor on 2025-06-18 and one on 2026-07-28
            // without either being configured for the other.
            .with_protocol_version(ProtocolVersion::LATEST)
            .with_server_info(
                Implementation::new("sourcetrail-mcp", env!("CARGO_PKG_VERSION"))
                    .with_title("Sourcetrail"),
            )
            .with_instructions(
                "Drive Sourcetrail's UI. Start with describe(): it lists the topics, and \
                 describe('<topic>') gives an area's usage notes and result shapes. \
                 instance_start spawns an app (labelled by its git checkout) so versions run \
                 side by side; tools take an optional `instance` (default: the one running \
                 instance). Read with ui_state; resolve names with nav_find; then \
                 nav_activate_node / nav_activate_file. app_state is the FSM. Tools were renamed \
                 with area prefixes: describe('<old name>') gives the new one.",
            )
    }
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt().with_writer(std::io::stderr).init();
    let service = SourcetrailServer::new(ManagerHandle::spawn()).serve(stdio()).await?;
    service.waiting().await?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeSet;

    /// Descriptions are the part every client pays for up front; the detail
    /// belongs in a `describe` topic.
    const MAX_DESCRIPTION_CHARS: usize = 220;

    fn registered() -> Vec<rmcp::model::Tool> {
        SourcetrailServer::tool_router().list_all()
    }

    #[test]
    fn registered_tools_and_catalog_agree() {
        let registered: BTreeSet<String> = registered().iter().map(|t| t.name.to_string()).collect();
        let documented: BTreeSet<String> =
            catalog::TOPICS.iter().flat_map(|t| t.tools.iter().map(|s| s.to_string())).collect();
        assert_eq!(
            registered, documented,
            "every registered tool must belong to exactly one describe topic, and every topic tool must exist"
        );
    }

    #[test]
    fn options_are_named_in_any_case_and_refused_with_the_list() {
        let directions = [("back", false), ("forward", true)];
        assert!(one_of("direction", " Forward ", &directions).unwrap());
        let refused = one_of("direction", "up", &directions).unwrap_err().message.to_string();
        assert_eq!(refused, "direction must be back or forward, not \"up\"");
        let buttons = [("left", 1), ("right", 2), ("middle", 3), ("none", 0)];
        let refused = one_of("button", "x", &buttons).unwrap_err().message.to_string();
        assert_eq!(refused, "button must be left, right, middle or none, not \"x\"");
    }

    #[test]
    fn tool_names_carry_an_area_prefix() {
        const PREFIXES: &[&str] = &["instance_", "ui_", "nav_", "graph_", "events_", "logs_", "project_", "machine_"];
        for t in registered() {
            let name = t.name.as_ref();
            assert!(
                name == "describe" || PREFIXES.iter().any(|p| name.starts_with(p)),
                "tool {name} has no area prefix"
            );
        }
    }

    #[test]
    fn descriptions_are_one_line_with_a_result_shape() {
        for t in registered() {
            let d = t.description.as_deref().unwrap_or_default();
            assert!(d.chars().count() <= MAX_DESCRIPTION_CHARS, "{}: description is {} chars", t.name, d.chars().count());
            assert!(!d.contains('\n'), "{}: description spans lines", t.name);
            assert!(d.contains("Returns"), "{}: description does not say what it returns", t.name);
        }
    }

    #[test]
    fn no_former_name_is_still_registered() {
        let registered: BTreeSet<String> = registered().iter().map(|t| t.name.to_string()).collect();
        for (old, _) in catalog::FORMER_NAMES {
            assert!(!registered.contains(*old), "{old} is still registered under its former name");
        }
    }
}
