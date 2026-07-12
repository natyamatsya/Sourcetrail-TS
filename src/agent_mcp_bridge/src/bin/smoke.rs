//! Transport smoke test — the Rust analogue of the C++ `prototype/cmd_roundtrip`.
//! Connects to a running Sourcetrail (`--agent-control`) and exercises the core
//! `Bridge` without the MCP SDK, so the wire layer can be validated on its own.
//!
//!   sourcetrail-mcp-smoke                  # get_ui_state
//!   sourcetrail-mcp-smoke status           # channel health (read-only)
//!   sourcetrail-mcp-smoke events [since]   # poll_events once (cursor = since seq)
//!   sourcetrail-mcp-smoke watch  [secs]    # stream events for N seconds (default 10)
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

    // `watch` streams inline for a while (a persistent bridge, like the MCP server).
    if cmd == Some("watch") {
        return watch(&mut bridge, arg2.parse().unwrap_or(10));
    }

    let out = match cmd {
        Some("status") => bridge.status(),
        Some("events") => bridge.poll_events(arg2.parse().unwrap_or(0)),
        Some("activate") => bridge.activate_node(None, arg2.parse().unwrap_or(0))?,
        Some("invoke") => invoke_first(&mut bridge, arg2)?,
        Some("capture") => capture_first(&mut bridge, arg2)?,
        Some("load") => load_and_wait(&mut bridge, arg2)?,
        Some("search") => bridge.search(arg2)?,
        Some("find") => bridge.find_element(arg2)?,
        Some("snapshot") => bridge.get_snapshot(false)?,
        _ => bridge.get_ui_state()?,
    };

    println!("{}", serde_json::to_string_pretty(&out)?);
    Ok(())
}

/// Snapshot the UI, find the first element advertising `action`, and invoke it on
/// its `ref` — the full structural-control round-trip (snapshot ref -> resolve ->
/// doAction). Demonstrates find-then-act.
/// Extract `(object_name, path)` from a snapshot node's `ref`.
fn ref_of(node: &Value) -> (String, Vec<(String, String, u32)>) {
    let r = &node["ref"];
    let object_name = r["object_name"].as_str().unwrap_or("").to_string();
    let path = r["path"]
        .as_array()
        .map(|steps| {
            steps
                .iter()
                .map(|s| {
                    (
                        s["role"].as_str().unwrap_or("").to_string(),
                        s["name"].as_str().unwrap_or("").to_string(),
                        s["index"].as_u64().unwrap_or(0) as u32,
                    )
                })
                .collect()
        })
        .unwrap_or_default();
    (object_name, path)
}

fn invoke_first(bridge: &mut Bridge, action: &str) -> Result<Value> {
    let snap = bridge.get_snapshot(false)?;
    let node = find_node_with_action(&snap, action)
        .ok_or_else(|| anyhow::anyhow!("no element advertising action '{action}' in the snapshot"))?;
    let (object_name, path) = ref_of(&node);
    eprintln!(
        "invoking '{action}' on {} (\"{}\")",
        node["role"].as_str().unwrap_or(""),
        node["name"].as_str().unwrap_or("")
    );
    bridge.invoke_action(&object_name, &path, action, "")
}

/// Screenshot the first element advertising `action`; decode + save the PNG.
fn capture_first(bridge: &mut Bridge, action: &str) -> Result<Value> {
    use base64::Engine as _;
    let snap = bridge.get_snapshot(false)?;
    let node = find_node_with_action(&snap, action)
        .ok_or_else(|| anyhow::anyhow!("no element advertising action '{action}' in the snapshot"))?;
    let (object_name, path) = ref_of(&node);
    eprintln!("capturing {} (\"{}\")", node["role"].as_str().unwrap_or(""), node["name"].as_str().unwrap_or(""));
    let out = bridge.capture_element(&object_name, &path, false)?;
    if let Some(b64) = out.pointer("/frame/image_base64").and_then(Value::as_str) {
        let png = base64::engine::general_purpose::STANDARD.decode(b64).unwrap_or_default();
        std::fs::write("/tmp/element_capture.png", &png).ok();
        eprintln!("saved {} PNG bytes to /tmp/element_capture.png", png.len());
    }
    // Drop the huge base64 blob from the printed result.
    let mut trimmed = out.clone();
    if let Some(f) = trimmed.get_mut("frame").and_then(Value::as_object_mut) {
        f.remove("image_base64");
    }
    Ok(trimmed)
}

fn find_node_with_action(node: &Value, action: &str) -> Option<Value> {
    if let Some(roots) = node.get("roots").and_then(Value::as_array) {
        return roots.iter().find_map(|r| find_node_with_action(r, action));
    }
    let has = node
        .get("actions")
        .and_then(Value::as_array)
        .is_some_and(|a| a.iter().any(|x| x.as_str() == Some(action)));
    if has {
        return Some(node.clone());
    }
    node.get("children")
        .and_then(Value::as_array)
        .and_then(|kids| kids.iter().find_map(|c| find_node_with_action(c, action)))
}

/// Stream events from a persistent bridge for `seconds`, printing each new event
/// as one JSON line and advancing the seq cursor — the observation loop the MCP
/// server runs, but inline. Trigger commands from another invocation to see them.
fn watch(bridge: &mut Bridge, seconds: u64) -> Result<()> {
    let deadline = Instant::now() + Duration::from_secs(seconds);
    let mut cursor = 0u64;
    while Instant::now() < deadline {
        let out = bridge.poll_events(cursor);
        if let Some(events) = out.get("events").and_then(Value::as_array) {
            for e in events {
                println!("{}", serde_json::to_string(e).unwrap_or_default());
            }
        }
        cursor = out.get("latest_seq").and_then(Value::as_u64).unwrap_or(cursor);
        sleep(Duration::from_millis(300));
    }
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
