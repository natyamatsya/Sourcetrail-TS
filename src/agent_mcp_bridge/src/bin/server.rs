//! `sourcetrail-mcp` — the MCP server. Exposes the running app's control channels
//! as MCP tools over stdio (the transport an MCP client such as Claude Code
//! launches). Requires the `mcp` feature.
//!
//! NOTE: the rmcp surface below targets rmcp ~0.8 (tool_router / tool macros,
//! stdio transport). Reconcile with the pinned version on the first
//! `cargo build --features mcp` — the actor and Bridge layers underneath are
//! version-independent.
//!
//! Design: context/DESIGN_AGENT_MCP_BRIDGE.md.

use std::sync::mpsc;

use agent_mcp_bridge::Bridge;
use anyhow::anyhow;
use serde_json::Value;
use tokio::sync::oneshot;

use rmcp::{
    handler::server::{router::tool::ToolRouter, wrapper::Parameters},
    model::{CallToolResult, Content, Implementation, ProtocolVersion, ServerCapabilities, ServerInfo},
    tool, tool_handler, tool_router,
    transport::stdio,
    ErrorData as McpError, ServerHandler, ServiceExt,
};

// --- Bridge actor -----------------------------------------------------------
// Route is not assumed Sync, so the Bridge lives on one dedicated OS thread and
// serves closures sent from the async rmcp handlers.

type Job = Box<dyn FnOnce(&mut Bridge) -> anyhow::Result<Value> + Send>;

#[derive(Clone)]
struct BridgeHandle {
    tx: mpsc::Sender<(Job, oneshot::Sender<anyhow::Result<Value>>)>,
}

impl BridgeHandle {
    fn spawn() -> anyhow::Result<Self> {
        let mut bridge = Bridge::connect()?;
        let (tx, rx) = mpsc::channel::<(Job, oneshot::Sender<anyhow::Result<Value>>)>();
        std::thread::spawn(move || {
            while let Ok((job, reply)) = rx.recv() {
                let _ = reply.send(job(&mut bridge));
            }
        });
        Ok(Self { tx })
    }

    async fn call<F>(&self, f: F) -> Result<Value, McpError>
    where
        F: FnOnce(&mut Bridge) -> anyhow::Result<Value> + Send + 'static,
    {
        let (rtx, rrx) = oneshot::channel();
        self.tx
            .send((Box::new(f), rtx))
            .map_err(|_| McpError::internal_error("bridge thread gone", None))?;
        rrx.await
            .map_err(|_| McpError::internal_error("bridge dropped reply", None))?
            .map_err(|e| McpError::internal_error(e.to_string(), None))
    }
}

fn ok_json(v: Value) -> Result<CallToolResult, McpError> {
    Ok(CallToolResult::success(vec![Content::text(
        serde_json::to_string_pretty(&v).unwrap_or_else(|_| v.to_string()),
    )]))
}

// --- Tool argument schemas --------------------------------------------------

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
struct QueryArgs {
    /// Human search string (symbol name, file name, …).
    query: String,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
struct ActivateNodeArgs {
    /// Preferred: the element's session-stable serialized_name (from find_element).
    #[serde(default)]
    serialized_name: Option<String>,
    /// Fast path: the session-local node id (e.g. from a prior search match).
    #[serde(default)]
    node_id: u64,
}

#[derive(serde::Deserialize, rmcp::schemars::JsonSchema)]
#[schemars(crate = "rmcp::schemars")]
struct PathArgs {
    /// Absolute path (project file for load_project, source file for activate_file).
    path: String,
}

// --- MCP server -------------------------------------------------------------

#[derive(Clone)]
struct SourcetrailServer {
    bridge: BridgeHandle,
    tool_router: ToolRouter<SourcetrailServer>,
}

#[tool_router]
impl SourcetrailServer {
    fn new(bridge: BridgeHandle) -> Self {
        Self { bridge, tool_router: Self::tool_router() }
    }

    #[tool(description = "Return the current UI state: project, app_state (FSM), active nodes, visible graph, code view, search matches, error counts.")]
    async fn get_ui_state(&self) -> Result<CallToolResult, McpError> {
        ok_json(self.bridge.call(|b| b.get_ui_state()).await?)
    }

    #[tool(description = "Search the indexed code base; returns matches (text, node_ids, score).")]
    async fn search(&self, Parameters(a): Parameters<QueryArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.bridge.call(move |b| b.search(&a.query)).await?)
    }

    #[tool(description = "Resolve a human query to the best-matching element(s), ranked (exact > prefix > contains). Use before activate_node.")]
    async fn find_element(&self, Parameters(a): Parameters<QueryArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.bridge.call(move |b| b.find_element(&a.query)).await?)
    }

    #[tool(description = "Activate a node (by serialized_name or node_id) and return the resulting UI state.")]
    async fn activate_node(&self, Parameters(a): Parameters<ActivateNodeArgs>) -> Result<CallToolResult, McpError> {
        ok_json(
            self.bridge
                .call(move |b| b.activate_node(a.serialized_name.as_deref(), a.node_id))
                .await?,
        )
    }

    #[tool(description = "Open a source file in the code view and return the resulting UI state.")]
    async fn activate_file(&self, Parameters(a): Parameters<PathArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.bridge.call(move |b| b.activate_file(&a.path)).await?)
    }

    #[tool(description = "Load a Sourcetrail project (.srctrlprj). Kicks off indexing; poll get_ui_state.app_state for the transition to Ready.")]
    async fn load_project(&self, Parameters(a): Parameters<PathArgs>) -> Result<CallToolResult, McpError> {
        ok_json(self.bridge.call(move |b| b.load_project(&a.path)).await?)
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
                "Drive Sourcetrail's UI. Read with get_ui_state; resolve names with \
                 find_element; then activate_node / activate_file. app_state is the FSM \
                 (NoProject/Loading/Indexing/Ready/Busy) — most commands need a project."
                    .into(),
            ),
        }
    }
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt().with_writer(std::io::stderr).init();

    let bridge = BridgeHandle::spawn().map_err(|e| anyhow!("connect to Sourcetrail: {e}"))?;
    let service = SourcetrailServer::new(bridge).serve(stdio()).await?;
    service.waiting().await?;
    Ok(())
}
