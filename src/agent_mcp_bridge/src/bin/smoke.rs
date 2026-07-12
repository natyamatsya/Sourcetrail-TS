//! Transport smoke test — the Rust analogue of the C++ `prototype/cmd_roundtrip`.
//! Connects to a running Sourcetrail (`--agent-control`) and exercises the core
//! `Bridge` without the MCP SDK, so the wire layer can be validated on its own.
//!
//!   sourcetrail-mcp-smoke                  # get_ui_state
//!   sourcetrail-mcp-smoke status           # channel health (read-only)
//!   sourcetrail-mcp-smoke load <project>   # load_project, then wait until it opens
//!   sourcetrail-mcp-smoke search <query>   # search
//!   sourcetrail-mcp-smoke find   <query>   # find_element (ranked)
//!   sourcetrail-mcp-smoke snapshot         # get_snapshot (accessibility tree)
//!   sourcetrail-mcp-smoke reset [--force]  # wipe the channel segments (sledgehammer)
//!
//! `SOURCETRAIL_AGENT_INSTANCE` targets a namespaced app (empty = default). The
//! app holds project state across bridge connections, so a `load` in one
//! invocation leaves the project open for a `search`/`snapshot` in the next.

use std::thread::sleep;
use std::time::{Duration, Instant};

use agent_mcp_bridge::protocol::channel;
use agent_mcp_bridge::Bridge;
use anyhow::{bail, Result};
use libipc::channel::Route;
use serde_json::{json, Value};

fn main() -> Result<()> {
    let args: Vec<String> = std::env::args().collect();
    let cmd = args.get(1).map(String::as_str);
    let arg2 = args.get(2).map(String::as_str).unwrap_or("");
    // SOURCETRAIL_AGENT_INSTANCE mirrors the app's --agent-instance so the smoke
    // client can target a namespaced app (empty = default st.agent.*).
    let instance = std::env::var("SOURCETRAIL_AGENT_INSTANCE").unwrap_or_default();

    // `reset` is a maintenance op on the shm segments themselves — no live app or
    // connection required (and it may be exactly what you run when none is up).
    if cmd == Some("reset") {
        return reset(&instance, args.iter().any(|a| a == "--force"));
    }

    let mut bridge = Bridge::connect_instance(&instance)?;

    let out = match cmd {
        Some("status") => bridge.status(),
        Some("load") => load_and_wait(&mut bridge, arg2)?,
        Some("search") => bridge.search(arg2)?,
        Some("find") => bridge.find_element(arg2)?,
        Some("snapshot") => bridge.get_snapshot(false)?,
        _ => bridge.get_ui_state()?,
    };

    println!("{}", serde_json::to_string_pretty(&out)?);
    Ok(())
}

/// Wipe an instance's channel segments via `clear_storage`. A **sledgehammer**:
/// unlike the dead-connection reaper (which only reclaims *dead* peers), this
/// evicts live app/bridge connections too — so it refuses without `--force`. Use
/// it to clear owner-less phantom bits the reaper can't (e.g. left by pre-reaper
/// builds), with no app attached.
fn reset(instance: &str, force: bool) -> Result<()> {
    let names = [
        channel::cmd(instance),
        channel::events(instance),
        channel::state(instance),
        channel::frames(instance),
        channel::snapshot(instance),
    ];
    if !force {
        eprintln!("`reset` would wipe these shared-memory segments:");
        for n in &names {
            eprintln!("    {n}");
        }
        eprintln!();
        eprintln!("This is a sledgehammer — it ALSO evicts any live app/bridge on them.");
        eprintln!("The reaper reclaims real phantoms automatically; use reset only to clear");
        eprintln!("owner-less bits left by pre-reaper builds, with no app attached.");
        eprintln!("Re-run with --force to proceed.");
        bail!("refused: pass --force to reset");
    }
    for n in &names {
        Route::clear_storage(n);
        println!("cleared {n}");
    }
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
