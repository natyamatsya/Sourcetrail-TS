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
