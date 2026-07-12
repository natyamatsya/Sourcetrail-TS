//! Transport smoke test — the Rust analogue of the C++ `prototype/cmd_roundtrip`.
//! Connects to a running Sourcetrail (`--agent-control`) and exercises the core
//! `Bridge` without the MCP SDK, so the wire layer can be validated on its own.
//!
//!   sourcetrail-mcp-smoke                  # get_ui_state
//!   sourcetrail-mcp-smoke search <query>   # search
//!   sourcetrail-mcp-smoke find   <query>   # find_element (ranked)

use agent_mcp_bridge::Bridge;

fn main() -> anyhow::Result<()> {
    let args: Vec<String> = std::env::args().collect();
    let mut bridge = Bridge::connect()?;

    let out = match args.get(1).map(String::as_str) {
        Some("search") => bridge.search(args.get(2).map(String::as_str).unwrap_or(""))?,
        Some("find") => bridge.find_element(args.get(2).map(String::as_str).unwrap_or(""))?,
        _ => bridge.get_ui_state()?,
    };

    println!("{}", serde_json::to_string_pretty(&out)?);
    Ok(())
}
