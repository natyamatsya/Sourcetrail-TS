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
| `instance_*` | `instance_start`, `instance_stop`, `instance_list`, `instance_handshake`, `instance_debug`, `instance_debug_command`, `instance_debug_dump` |
| `ui_*` | `ui_state`, `ui_frame`, `ui_snapshot`, `ui_query`, `ui_invoke`, `ui_input`, `ui_capture` |
| `nav_*` | `nav_search`, `nav_find`, `nav_activate_node`, `nav_activate_file`, `nav_activate_tab`, `nav_scroll_to_line`, `nav_history` |
| `graph_*` | `graph_expand`, `graph_hide`, `graph_split`, `graph_trail` |
| `events_*` | `events_poll`, `events_history` |
| `logs_*` | `logs_forward` |
| `project_*` | `project_open` |
| `machine_*` | `machine_state`, `machine_send` |

Tools that change the app (`nav_activate_*`, `nav_scroll_to_line`, `nav_history`,
`graph_*`) **auto-observe**: they return the `ui_state` once the app reports the command's
effects settled. `app_state` is the FSM the agent keys off; `dialog` says what
waits for an answer in front of the app.

```text
NoProject ──load──▶ Loading ──project open, first tab activated──▶ Ready
                       │  (or: quiet with no tab to open)            │
                       └──────────────── Indexing ◀──index──────────┘
                                            └──done──▶ Loading ▶ Ready
```

`Loading` lasts from a load request until the project is open and the tab the load
opens has shown the overview, so nothing of the load can land after the agent's
first navigation; `nav_*` is refused meanwhile. `project_open` (and
`instance_start { project }`) wait for it and return the state to start from, so
an agent never needs to sleep after opening a project.

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
- runs **without animations** (`--no-animations`, the stored setting untouched)
  unless `animations: true`: settling does not wait for a graph transition, so a
  frame taken after a command would catch it midway;
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

From protocol v6 the questions and the indexing dialogs are state machines, which is
the better way to answer them: see below.

## State machines

What waits for a person is a state machine in the app (protocol v6): `indexing` (the
mode choice, progress, the report) and `question` (the boxes the app asks and waits
on). So are `app` (starting -> running -> closing) and `project` (no_project -> loading
-> opening -> ready), which `app_state` is derived from; a project load asked for while
the app starts waits for it. `activation` (idle -> activating -> rendering -> idle)
follows an activation until the graph and code views have drawn it, animations included:
`Settled` waits for it, and so does `ui_invoke` when its press started one. A dialog shows a state, and its buttons send the machine events; `machine_send`
sends the same events, and the dialog closes when an agent answered it. Every move is
a `MachineTransition` in `events_poll`, with its source (`ui`, `agent` or `app`).

```text
machine_state {}                                          -> each machine's state, accepted events, context
machine_send  { machine: "indexing", event: "start", until: ["idle"], timeout_s: 300 }
machine_send  { machine: "question", event: "answer", args: { label: "Reindex" } }
```

An indexing run ends by reopening the project on its new index, so `until: ["idle"]` on
`indexing` also waits for the project to be `ready` again: navigation can follow at once.

`project_open` on a project whose load asks first ("the index is outdated:
reindex?") returns at once with the question instead of waiting for the load.
`describe('machines')` has both lifecycles.

## Synthetic input

`ui_input` posts mouse, wheel and key events to the app's own widgets (protocol v5),
for what the accessibility tree does not reach: graph nodes and edges, context menus,
hover, ctrl+wheel zoom. Aim with `at_frame: [x, y]`, a pixel of the last `ui_frame`
(its result carries the frame's origin and scale), with `at` (global coordinates), or
with a `ref`; send a `gesture` (`click`, `double_click`, `right_click`, `hover`) or a
list of `events`. The result names the widget that got the input and, like a
navigation command, the state once the app settled: click a graph node and
`state.active_nodes` is that node. `describe('input')` has the details. Dragging a
dock separator does not work: the docks ask the platform which buttons are down.

## Debugging

Every instance `instance_start` launches gets the platform debugger attached right
after the handshake (`debugger: false` skips it): cdb on Windows (Debugging Tools
for Windows, or `CDB_PATH`), lldb on macOS. The bridge drives it itself: cdb
through its console, lldb through its MCP protocol server.

- **Crash** (an exception nobody handled: access violation, abort/qFatal, stack
  overflow, an unhandled C++ exception): the debugger writes a full dump beside the
  instance's console file and holds the app at the fault. `instance_list` shows
  `stopped` (the exception and frame) and `dump_written`; calls on the instance
  fail at once with the stop.
- **Commands**: `instance_debug_command { command }` in the debugger's own
  language, returning `{ output, stopped }`. A running app is stopped for the
  command and resumed after. cdb: `.ecxr; kb`, `~*k`, `!analyze -v`, `.frame N; dv`,
  `bp module!Class::method`, `g` on its own to resume.
- **Live**: `instance_debug { break_in: true }` stops the app; resume it before
  driving the UI again.
- **Post-mortem**: `instance_debug_dump { path }` opens a dump as an id for
  `instance_debug_command`.
- **Symbols**: a Release build shows bare offsets. Build the `reldbg` preset (with
  `-D VCPKG_MANIFEST_FEATURES=cxx-indexer` for the C++ indexer) for Sourcetrail's
  own frames; system modules come from Microsoft's symbol server unless
  `_NT_SYMBOL_PATH` is set.
- Dumps are pruned at `instance_start` to the 3 newest (`SOURCETRAIL_MCP_KEEP_DUMPS`).
- `tests/debugger.rs` checks crash, abort, normal exit, commands while running,
  break-in and post-mortem against the installed debugger, with
  `examples/crashme.rs`; it is skipped where none is installed. The lldb side has
  not been run yet.

A worked example: opening a project 150–200 ms after pressing Back crashed
Sourcetrail about every other time. Driven headless on the `reldbg` build with
the debugger attached, the crash came back as a stop with a dump, and `.ecxr; kb`
named `QtCodeNavigator::scrollTo`'s deferred lambda calling into a destroyed view
(`_purecall`), a context-less `QTimer::singleShot`. The same 20-run sequence then
showed the fix: 10 crashes before, none after.

## Status

Verified against the rel-cxx and reldbg builds on Windows, headless. Not yet
covered here: an agent log layer with query tools, synthetic mouse/keyboard input
for the graph view, and a per-request history.
