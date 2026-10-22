# sourcetrail-mcp — agent MCP bridge

A standalone process that exposes a running Sourcetrail (agent control is on by
default in agent builds) as an **MCP server**, so an AI agent (Claude Code, or any
MCP client) can drive and observe the UI. It translates MCP tool calls ⇄ FlatBuffers
over thoth-ipc shared memory — no filesystem for transport.

Design: [`context/DESIGN_AGENT_MCP_BRIDGE.md`](../../context/DESIGN_AGENT_MCP_BRIDGE.md).
App side: [`src/lib_core/component/controller/agent_control/`](../lib_core/component/controller/agent_control/).
Wire contracts: [`abi-schemas/ipc-mcp-agent/`](../../abi-schemas/ipc-mcp-agent/)
(`agent_common.fbs` keeps the protocol version log).

## Layering

| Module | Depends on | Verifiable by |
|---|---|---|
| `schema` | `flatbuffers` (generated from the shared `*.fbs` at build time) | `cargo check` |
| `protocol` | `schema`, `serde_json` — `Command` builders + envelope decoders | `cargo test` |
| `ipc::Bridge` | `thoth-ipc`, `protocol` — connect routes, act-and-observe ops | `cargo check` + `smoke` against a live app |
| `instance` | `ipc::Bridge` — spawn, attach, resolve and stop app instances | `cargo test` (profiles, Qt lookup) |
| `catalog` | `serde_json` — the `describe` topics (usage notes + result shapes) | `cargo test` |
| `bin/server` (`mcp` feature) | `rmcp`, `tokio` — the stdio MCP server | `cargo test --features mcp` |

The wire/transport half builds without the MCP SDK, so it can be validated on its
own; `rmcp` is pulled only by the `mcp` feature. The server targets rmcp 3.5, which
serves both MCP lifecycles: a client that opens with `initialize` negotiates a
session, and one speaking 2026-07-28 (`server/discover`, or any request carrying the
required `_meta`) is served without one.

## Build & run

```sh
# core + transport (no MCP SDK):
cargo check
cargo test                                       # protocol, catalog, instance tests; no app needed
cargo run --bin sourcetrail-mcp-smoke            # ui_state against a running app
cargo run --bin sourcetrail-mcp-smoke -- find parseArgs

# the MCP server:
cargo build --release --features mcp --bin sourcetrail-mcp
cargo test --features mcp                        # + tool/catalog consistency
```

`build.rs` generates the FlatBuffers Rust bindings with `flatc` (found via `$FLATC`,
else the project's vcpkg copy under `.build/*/vcpkg_installed/*/tools/flatbuffers/`,
else `PATH`). On Windows flatc sits under the *host* triplet, which the glob misses:
set `FLATC` to `.build/<preset>/vcpkg_installed/x64-windows/tools/flatbuffers/flatc.exe`.

## Use from an MCP client

```jsonc
// .mcp.json
{
  "mcpServers": {
    "sourcetrail": { "command": "/path/to/sourcetrail-mcp" }
  }
}
```

The agent either starts instances itself (`instance_start`) or attaches to a
Sourcetrail you started (`--agent-instance <id>`, or no id for the default channels).

## Tools

Tools are grouped by area prefix, so a client that searches or defers tool
definitions finds a whole area with one query:

| Area | Tools |
|---|---|
| discovery | `describe` |
| `instance_*` | `instance_start`, `instance_stop`, `instance_list`, `instance_handshake` |
| `ui_*` | `ui_state`, `ui_frame`, `ui_snapshot`, `ui_query`, `ui_invoke`, `ui_capture` |
| `nav_*` | `nav_search`, `nav_find`, `nav_activate_node`, `nav_activate_file`, `nav_activate_tab`, `nav_scroll_to_line`, `nav_history` |
| `events_*` | `events_poll` |
| `logs_*` | `logs_forward` |
| `project_*` | `project_open` |

Tools that change the app (`nav_activate_*`, `nav_scroll_to_line`, `nav_history`)
**auto-observe**: they return the `ui_state` once the app reports the command's
effects settled. `app_state` (NoProject/Indexing/Ready) is the FSM the agent keys
off; `dialog` says what waits for an answer in front of the app.

The tools were renamed with these prefixes (`get_ui_state` → `ui_state`, `search` →
`nav_search`, `start_instance` → `instance_start`, ...). MCP has no aliases, so
`describe('<former name>')` answers with the new name.

### Progressive discovery

- **Descriptions are one line**: when to use the tool, plus its result shape.
- **`describe`** is the entry point. With no argument it lists the topics; with one
  (`describe('nav')`, or a tool name) it returns that area's usage notes and exact
  result shapes. The topics live in [`src/catalog.rs`](src/catalog.rs).
- Unknown arguments are rejected, naming the field.

Adding a tool: give it an area prefix, a one-line description (at most 220
characters) that says what it returns, and list it under exactly one topic in
`catalog.rs`. The server's tests fail otherwise.

## Instances

`instance_start { bin }` spawns Sourcetrail with `--agent-instance <id>` (the id
defaults to the binary's git label `<branch>-<hash>`, so two builds run side by side)
and returns once the app answers the handshake. A spawned instance:

- runs **headless** (`QT_QPA_PLATFORM=offscreen`) unless `headless: false`;
- runs on the **Qt it was built with**: the prefix comes from `Qt6_DIR` in the
  `CMakeCache.txt` of the build tree the binary sits in (or `SOURCETRAIL_MCP_QT_DIR`),
  its `bin` goes first on `PATH` and its `plugins` on `QT_PLUGIN_PATH`;
- runs on an **isolated settings profile** (`--user-data-dir`):
  `$SOURCETRAIL_MCP_HOME/profiles/<name>`, `"agent"` unless `profile` names another,
  persistent across runs; `profile: "user"` uses your own settings, and `fresh: true`
  a throwaway copy of the profile;
- has stdin closed and its **stdout/stderr in a console file**
  (`$SOURCETRAIL_MCP_HOME/logs/<id>-<ms>.console.txt`) — never on the bridge's own
  stdout, which carries the MCP protocol.

`$SOURCETRAIL_MCP_HOME` defaults to `sourcetrail-mcp` in the per-user local data
directory. An empty `instance` argument means the one running instance; with none
or several it addresses the unnamespaced `st.agent.*` channels, i.e. an app you
started yourself.

## Seeing and answering dialogs

`ui_frame` returns the screen as an MCP image: every visible window composited,
dialogs included, with the windows named in the result. Some of Sourcetrail's
questions ("reindex the project?") block the thread agent commands run on until they
are answered. From agent-control protocol v3 the GUI-only commands (`ui_frame`,
`ui_snapshot`, `ui_query`, `ui_invoke`, `ui_capture`, `instance_handshake`) bypass
that thread, so the agent can still see such a box and press its button while
`ui_state` waits.

## Status

Verified against the rel-cxx build on Windows, headless. Not yet covered here: an
agent log layer with query tools, synthetic mouse/keyboard input for the graph
view, a per-request history, and attaching a debugger for crash dumps.
