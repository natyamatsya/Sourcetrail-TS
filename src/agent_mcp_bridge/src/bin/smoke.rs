//! Transport smoke test — the Rust analogue of the C++ `prototype/cmd_roundtrip`.
//! Connects to a running Sourcetrail (`--agent-control`) and exercises the core
//! `Bridge` without the MCP SDK, so the wire layer can be validated on its own.
//!
//!   sourcetrail-mcp-smoke                  # get_ui_state
//!   sourcetrail-mcp-smoke load <project>   # load_project, then wait until it opens
//!   sourcetrail-mcp-smoke search <query>   # search
//!   sourcetrail-mcp-smoke find   <query>   # find_element (ranked)
//!   sourcetrail-mcp-smoke snapshot         # get_snapshot (accessibility tree)
//!
//! The app holds project state across bridge connections, so a `load` in one
//! invocation leaves the project open for a `search`/`snapshot` in the next.

use std::thread::sleep;
use std::time::{Duration, Instant};

use agent_mcp_bridge::Bridge;
use serde_json::{json, Value};

fn main() -> anyhow::Result<()> {
    let args: Vec<String> = std::env::args().collect();
    let arg2 = args.get(2).map(String::as_str).unwrap_or("");
    let mut bridge = Bridge::connect()?;

    let out = match args.get(1).map(String::as_str) {
        Some("load") => load_and_wait(&mut bridge, arg2)?,
        Some("search") => bridge.search(arg2)?,
        Some("find") => bridge.find_element(arg2)?,
        Some("snapshot") => bridge.get_snapshot(false)?,
        _ => bridge.get_ui_state()?,
    };

    println!("{}", serde_json::to_string_pretty(&out)?);
    Ok(())
}

/// Load a project, then poll `ui_state` until it leaves `NoProject`. Loading kicks
/// off asynchronously (see `Bridge::load_project`), so the ack alone doesn't mean
/// the project is open — poll so the app is left ready for a follow-up command, and
/// report indexing status without blocking on a full reindex.
fn load_and_wait(bridge: &mut Bridge, path: &str) -> anyhow::Result<Value> {
    if path.is_empty() {
        anyhow::bail!("usage: sourcetrail-mcp-smoke load <project-file.srctrl.toml>");
    }
    let ack = bridge.load_project(path)?;

    let deadline = Instant::now() + Duration::from_secs(30);
    loop {
        let state = bridge.get_ui_state()?;
        let app_state = state.get("app_state").and_then(Value::as_str).unwrap_or("");
        if app_state != "NoProject" {
            return Ok(json!({ "ack": ack, "state": state }));
        }
        if Instant::now() >= deadline {
            return Ok(json!({ "ack": ack, "state": state, "note": "timed out waiting to open" }));
        }
        sleep(Duration::from_millis(500));
    }
}
