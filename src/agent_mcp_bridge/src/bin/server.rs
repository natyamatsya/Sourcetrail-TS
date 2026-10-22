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
use std::sync::mpsc;

use agent_mcp_bridge::catalog;
use agent_mcp_bridge::instance::{InstanceManager, StartOpts};
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
        rrx.await
            .map_err(|_| McpError::internal_error("manager dropped reply", None))?
            .map_err(|e| McpError::internal_error(format!("{e:#}"), None))
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

fn default_true() -> bool {
    true
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
struct DescribeArgs {
    /// Topic (or tool name); empty lists the topics.
    #[serde(default)]
    topic: String,
}

// --- MCP server -------------------------------------------------------------

// No `tool_router` field: `#[tool_handler]` calls `Self::tool_router()` itself,
// so a stored copy is one the server never reads.
//
// Tool names carry an area prefix (instance_, ui_, nav_, events_, logs_,
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

    #[tool(description = "List known instances. Returns { instances: [ { id, spawned, pid, running, exit_code?, bin?, console_file?, qt?, user_data_dir?, fresh? } | { id, start_failed } ] }.")]
    async fn instance_list(&self) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(|m| Ok(m.list())).await?)
    }

    #[tool(description = "Check an instance's protocol version against the bridge's. Returns { ok, bridge_protocol_version, app_protocol_version, app_version, skew: match|app_older|app_newer, wire }.")]
    async fn instance_handshake(&self, Parameters(a): Parameters<InstanceArg>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| Ok(m.get_or_attach(&a.instance)?.bridge().handshake())).await?)
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

    #[tool(description = "Open a Sourcetrail project (.srctrlprj / .srctrl.toml). Returns { ok, message } on acceptance; indexing follows, so wait for app_state Ready (events_poll).")]
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

    #[tool(description = "Navigate back or forward in the activation history, like the toolbar arrows. direction: back|forward. Returns the settled ui_state.")]
    async fn nav_history(&self, Parameters(a): Parameters<HistoryArgs>) -> Result<CallToolResult, McpError> {
        let forward = match a.direction.trim().to_ascii_lowercase().as_str() {
            "back" => false,
            "forward" => true,
            other => {
                return Err(McpError::invalid_params(
                    format!("direction must be \"back\" or \"forward\", not \"{other}\""),
                    None,
                ))
            }
        };
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
    fn tool_names_carry_an_area_prefix() {
        const PREFIXES: &[&str] = &["instance_", "ui_", "nav_", "events_", "logs_", "project_"];
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
