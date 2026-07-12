# sourcetrail-mcp — agent MCP bridge

A standalone process that exposes a running Sourcetrail (`--agent-control`) as an
**MCP server**, so an AI agent (Claude Code, or any MCP client) can drive and
observe the UI. It translates MCP tool calls ⇄ FlatBuffers over thoth-ipc shared
memory — no filesystem for transport.

Design: [`context/DESIGN_AGENT_MCP_BRIDGE.md`](../../context/DESIGN_AGENT_MCP_BRIDGE.md).
App side: [`src/lib/component/controller/agent_control/`](../lib/component/controller/agent_control/).

## Layering

| Module | Depends on | Verifiable by |
|---|---|---|
| `schema` | `flatbuffers` (generated from the shared `*.fbs` at build time) | `cargo check` |
| `protocol` | `schema`, `serde_json` — `Command` builders + envelope decoders | `cargo check` |
| `ipc::Bridge` | `thoth-ipc`, `protocol` — connect routes, act-and-observe ops | `cargo check` + `smoke` against a live app |
| `bin/server` (`mcp` feature) | `rmcp`, `tokio` — the stdio MCP server | `cargo build --features mcp` |

The wire/transport half builds without the MCP SDK, so it can be validated on its
own; `rmcp` is pulled only by the `mcp` feature.

## Build & run

```sh
# core + transport smoke test (no MCP SDK):
cargo check
cargo run --bin sourcetrail-mcp-smoke            # get_ui_state against a running app
cargo run --bin sourcetrail-mcp-smoke -- find parseArgs

# the MCP server:
cargo build --release --features mcp --bin sourcetrail-mcp
```

`build.rs` generates the FlatBuffers Rust bindings with `flatc` (found via `$FLATC`,
else the project's vcpkg copy under `.build/*/vcpkg_installed/*/tools/flatbuffers/`,
else `PATH`).

## Use from an MCP client

Run Sourcetrail with agent control, then point the client at the server binary:

```jsonc
// .mcp.json
{
  "mcpServers": {
    "sourcetrail": { "command": "/path/to/sourcetrail-mcp" }
  }
}
```

## Tools (MVP)

`get_ui_state`, `search`, `find_element`, `activate_node`, `activate_file`,
`load_project`. Mutating tools **auto-observe** (return the resulting `UiState`).
`app_state` (NoProject/Loading/Indexing/Ready/Busy) is the FSM the agent keys off.

## Status

Scaffold. Core (schema/protocol/ipc) is written against verified thoth-ipc +
flatbuffers APIs; the `rmcp` server targets rmcp ~0.8 and needs a first
`cargo build --features mcp` to reconcile the SDK surface. Not yet wired into the
CMake/Corrosion build (it is a separate process). Resources/subscriptions and
`get_frame` reassembly are follow-ups (Phase C.2–C.3).
