//! `sourcetrail-mcp` — the MCP server. Exposes the app's control channels as MCP
//! tools over stdio, and manages app lifecycle + multiple instances (for
//! comparison tests). Requires the `mcp` feature.
//!
//! NOTE: the rmcp surface targets rmcp 0.8.5 (verified with `cargo check
//! --features mcp`). Design: context/DESIGN_AGENT_MCP_BRIDGE.md.

use std::path::PathBuf;
use std::sync::mpsc;

use agent_mcp_bridge::instance::{InstanceManager, StartOpts};
use serde_json::{json, Value};
use tokio::sync::oneshot;

use rmcp::{
    handler::server::{router::tool::ToolRouter, wrapper::Parameters},
    model::{CallToolResult, Content, Implementation, ProtocolVersion, ServerCapabilities, ServerInfo},
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
        rrx.await
            .map_err(|_| McpError::internal_error("manager dropped reply", None))?
            .map_err(|e| McpError::internal_error(e.to_string(), None))
    }
}

fn ok_json(v: Value) -> Result<CallToolResult, McpError> {
    Ok(CallToolResult::success(vec![Content::text(
        serde_json::to_string_pretty(&v).unwrap_or_else(|_| v.to_string()),
    )]))
}

fn default_true() -> bool {
    true
}

// --- Tool argument schemas --------------------------------------------------
// Every driving tool takes an optional `instance` (default = the unnamespaced
// running app); the manager lazily attaches to it on first use.

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
struct InstanceArg {
    #[serde(default)]
    instance: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
struct RefPathStep {
    role: String,
    #[serde(default)]
    name: String,
    #[serde(default)]
    index: u32,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
struct ElementRefArg {
    #[serde(default)]
    object_name: String,
    #[serde(default)]
    path: Vec<RefPathStep>,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
struct InvokeActionArgs {
    /// The element to act on — pass a `ref` object from get_snapshot (object_name + path).
    #[serde(rename = "ref")]
    target: ElementRefArg,
    /// Accessible action name from the node's `actions` (e.g. "Press", "Toggle", "ShowMenu").
    #[serde(default)]
    action: String,
    /// Optional: set the element's editable text/value (line edits, combos).
    #[serde(default)]
    text: String,
    #[serde(default)]
    instance: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
struct CaptureElementArgs {
    /// The element to screenshot — pass a `ref` from get_snapshot.
    #[serde(rename = "ref")]
    target: ElementRefArg,
    /// Also return the element's properties (name/value/actions). (Reserved.)
    #[serde(default)]
    include_properties: bool,
    #[serde(default)]
    instance: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
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
struct QueryArgs {
    query: String,
    #[serde(default)]
    instance: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
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
struct PathArgs {
    path: String,
    #[serde(default)]
    instance: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
struct TabArgs {
    /// Tab id (from get_ui_state.tabs[].tab_id).
    tab_id: u64,
    #[serde(default)]
    instance: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
struct SnapshotArgs {
    /// Raw object tree (widgets + Q_PROPERTY bag) instead of the default accessibility tree.
    #[serde(default)]
    object_tree: bool,
    #[serde(default)]
    instance: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
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
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
struct KillArgs {
    id: String,
}

// --- MCP server -------------------------------------------------------------

#[derive(Clone)]
struct SourcetrailServer {
    mgr: ManagerHandle,
    tool_router: ToolRouter<SourcetrailServer>,
}

#[tool_router]
impl SourcetrailServer {
    fn new(mgr: ManagerHandle) -> Self {
        Self { mgr, tool_router: Self::tool_router() }
    }

    #[tool(description = "Return the current UI state (project, app_state FSM, active nodes, graph, code view, search matches, errors) of an instance.")]
    async fn get_ui_state(&self, Parameters(a): Parameters<InstanceArg>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| m.get_or_attach(&a.instance)?.bridge().get_ui_state()).await?)
    }

    #[tool(
        description = "Poll app events since a cursor. Returns {events, latest_seq}; pass latest_seq back as since_seq (start at 0). Events include AppStateChanged, IndexingStarted/Progress/Finished, NodesActivated, FileActivated, SearchCompleted, ErrorCountChanged, StatusChanged, TabsChanged, CommandResult. Use to await a transition (e.g. app_state -> Ready) instead of re-polling get_ui_state. A seq jump beyond the returned events means some were dropped — poll more often."
    )]
    async fn poll_events(&self, Parameters(a): Parameters<PollEventsArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| Ok(m.get_or_attach(&a.instance)?.bridge().poll_events(a.since_seq))).await?)
    }

    #[tool(description = "Search the indexed code base; returns matches (text, node_ids, score).")]
    async fn search(&self, Parameters(a): Parameters<QueryArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| m.get_or_attach(&a.instance)?.bridge().search(&a.query)).await?)
    }

    #[tool(description = "Resolve a human query to the best-matching element(s), ranked. Use before activate_node.")]
    async fn find_element(&self, Parameters(a): Parameters<QueryArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| m.get_or_attach(&a.instance)?.bridge().find_element(&a.query)).await?)
    }

    #[tool(description = "Activate a node (by serialized_name or node_id); returns the resulting UI state.")]
    async fn activate_node(&self, Parameters(a): Parameters<ActivateNodeArgs>) -> Result<CallToolResult, McpError> {
        ok_json(
            self.mgr
                .call(move |m| m.get_or_attach(&a.instance)?.bridge().activate_node(a.serialized_name.as_deref(), a.node_id))
                .await?,
        )
    }

    #[tool(description = "Open a source file in the code view; returns the resulting UI state.")]
    async fn activate_file(&self, Parameters(a): Parameters<PathArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| m.get_or_attach(&a.instance)?.bridge().activate_file(&a.path)).await?)
    }

    #[tool(description = "Switch to a specific tab by id (from get_ui_state.tabs); returns the resulting UI state.")]
    async fn activate_tab(&self, Parameters(a): Parameters<TabArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| m.get_or_attach(&a.instance)?.bridge().activate_tab(a.tab_id)).await?)
    }

    #[tool(description = "Capture the structural UI tree: every element's role, geometry, and supported actions, each with a ref you can target. Accessibility tree by default; object_tree=true for the raw widget/property tree.")]
    async fn get_snapshot(&self, Parameters(a): Parameters<SnapshotArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| m.get_or_attach(&a.instance)?.bridge().get_snapshot(a.object_tree)).await?)
    }

    #[tool(description = "Invoke an accessible action on a UI element (structural control). Pass a `ref` from get_snapshot plus an `action` from that node's `actions` (e.g. Press, Toggle, ShowMenu), or `text` to set an editable field. Returns { ok, message } — resolution failures (element not found / action not supported) are data, not errors.")]
    async fn invoke_action(&self, Parameters(a): Parameters<InvokeActionArgs>) -> Result<CallToolResult, McpError> {
        ok_json(
            self.mgr
                .call(move |m| {
                    let path: Vec<(String, String, u32)> =
                        a.target.path.iter().map(|s| (s.role.clone(), s.name.clone(), s.index)).collect();
                    m.get_or_attach(&a.instance)?.bridge().invoke_action(
                        &a.target.object_name,
                        &path,
                        &a.action,
                        &a.text,
                    )
                })
                .await?,
        )
    }

    #[tool(description = "Select UI elements by JSONPath (RFC 9535), evaluated server-side over the accessibility snapshot (same shape as get_snapshot). Returns a snapshot whose roots are the matches, each with a ref you can invoke/capture. Filters at the source — the full tree never crosses the wire. Example: $.roots..[?(@.role=='button')].")]
    async fn query_ui(&self, Parameters(a): Parameters<QueryArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| m.get_or_attach(&a.instance)?.bridge().query_ui(&a.query)).await?)
    }

    #[tool(description = "Screenshot a single UI element by its `ref` (from get_snapshot). Returns { ok, frame } where frame has width/height and image_base64 (PNG). Complements get_snapshot's structure with pixels.")]
    async fn capture_element(&self, Parameters(a): Parameters<CaptureElementArgs>) -> Result<CallToolResult, McpError> {
        ok_json(
            self.mgr
                .call(move |m| {
                    let path: Vec<(String, String, u32)> =
                        a.target.path.iter().map(|s| (s.role.clone(), s.name.clone(), s.index)).collect();
                    m.get_or_attach(&a.instance)?.bridge().capture_element(
                        &a.target.object_name,
                        &path,
                        a.include_properties,
                    )
                })
                .await?,
        )
    }

    #[tool(description = "Load a project (.srctrlprj); kicks off indexing — poll get_ui_state.app_state for Ready.")]
    async fn load_project(&self, Parameters(a): Parameters<PathArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| m.get_or_attach(&a.instance)?.bridge().load_project(&a.path)).await?)
    }

    #[tool(description = "Start (spawn) a new Sourcetrail instance. `id` defaults to the binary's git label (<branch>-<hash>). Returns the instance id to target with other tools.")]
    async fn start_instance(&self, Parameters(a): Parameters<StartArgs>) -> Result<CallToolResult, McpError> {
        ok_json(
            self.mgr
                .call(move |m| {
                    let id = m.start(StartOpts {
                        bin: PathBuf::from(a.bin),
                        id: a.id,
                        project: a.project,
                        headless: a.headless,
                    })?;
                    Ok(json!({ "id": id }))
                })
                .await?,
        )
    }

    #[tool(description = "Kill a spawned instance (or detach an attached one) and forget it.")]
    async fn kill_instance(&self, Parameters(a): Parameters<KillArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(move |m| { m.kill(&a.id)?; Ok(json!({ "killed": a.id })) }).await?)
    }

    #[tool(description = "List known instances (spawned or attached) and their ids.")]
    async fn list_instances(&self) -> Result<CallToolResult, McpError> {
        ok_json(self.mgr.call(|m| Ok(m.list())).await?)
    }
}

#[tool_handler]
impl ServerHandler for SourcetrailServer {
    fn get_info(&self) -> ServerInfo {
        ServerInfo {
            protocol_version: ProtocolVersion::LATEST,
            capabilities: ServerCapabilities::builder().enable_tools().build(),
            server_info: Implementation {
                name: "sourcetrail-mcp".into(),
                version: env!("CARGO_PKG_VERSION").into(),
                title: Some("Sourcetrail".into()),
                icons: None,
                website_url: None,
            },
            instructions: Some(
                "Drive Sourcetrail's UI. Tools take an optional `instance` id (default: the \
                 running app). start_instance spawns a new app (labelled by its git checkout) \
                 so you can compare versions side by side; kill_instance / list_instances manage \
                 the pool. Read with get_ui_state; resolve names with find_element; then \
                 activate_node / activate_file. app_state is the FSM."
                    .into(),
            ),
        }
    }
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt().with_writer(std::io::stderr).init();
    let service = SourcetrailServer::new(ManagerHandle::spawn()).serve(stdio()).await?;
    service.waiting().await?;
    Ok(())
}
