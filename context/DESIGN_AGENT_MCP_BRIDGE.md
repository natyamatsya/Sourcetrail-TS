# Design: Agent MCP bridge (Phase C)

Exposes the running Sourcetrail as an **MCP server** so an AI agent (Claude Code, or
any MCP client) can drive and observe the UI. It is the agent-facing half of the
control plane designed in [`DESIGN_AGENT_UI_CONTROL.md`](DESIGN_AGENT_UI_CONTROL.md);
the app-facing half (`AgentControlController` + thoth-ipc channels + FlatBuffers
contracts) already exists. The bridge is a **thin adapter**: MCP JSON-RPC ⇄ FlatBuffers
over thoth-ipc. It adds no new capability to the app — it translates and correlates.

## Topology

```
  Claude Code / MCP client
        │  MCP (JSON-RPC 2.0 over stdio)
        ▼
  ┌───────────────┐     st.agent.cmd    (route N→1)  Command  ─────────►
  │  MCP bridge   │     st.agent.events (route 1→N)  Event    ◄─────────  Sourcetrail
  │  (adapter)    │     st.agent.state  (channel)    UiState  ◄─────────  AgentControl-
  └───────────────┘     st.agent.frames (route 1→N)  Frame    ◄─────────  Controller
        thoth-ipc shared memory + FlatBuffers (no filesystem)
```

The bridge is a **standalone process**, not part of Sourcetrail. The app just runs with
`--agent-control`; the bridge connects to the same named channels. This keeps the app
free of MCP/JSON-RPC, lets the bridge restart independently, and matches how MCP clients
expect to launch a server (a stdio subprocess).

## Language / stack decision

The bridge must speak **both** MCP (mature SDKs in TS/Python) **and** thoth-ipc
(bindings in C++/Rust/Swift only) with FlatBuffers. That rules out a pure TS/Python
server (no thoth-ipc binding without FFI). Two viable stacks:

| Option | MCP | thoth-ipc | FlatBuffers | Notes |
|---|---|---|---|---|
| **Rust** *(recommended)* | `rmcp` (official Rust SDK) | Rust binding (exists) | `flatc --rust` | Fits existing Rust infra (Corrosion, turso_shim, rust indexer); real SDK handles the handshake/capabilities/schema. |
| **C++** | hand-rolled JSON-RPC (no mature C++ MCP SDK) | `libipc` directly | C++ headers already generated | Zero new toolchain; reuses the exact schemas + `cmd_roundtrip` transport code, but we own the MCP protocol surface. |

**Decision: Rust (`rmcp`).** The MCP handshake, capability negotiation, tool/resource
registration, and notifications are non-trivial and evolving — leaning on `rmcp` is worth
far more than the C++ code reuse. Rust is already in the build (Corrosion). The wire
contracts are identical, so the C++ route stays open as a fallback if ever needed.

Concretely: a new `src/agent_mcp_bridge/` Cargo crate (imported via the existing Corrosion
setup) depending on `rmcp`, the thoth-ipc Rust binding, and `flatc --rust`-generated types
from the same `schemas/*.fbs` (a build step mirroring the C++ `flatc --cpp` generation).

## MCP surface

### Tools (agent actions → `Command`)

Each tool maps 1:1 onto the `Command` union, assigns a fresh `request_id`, sends a
`CommandEnvelope` on `st.agent.cmd`, and awaits the correlated `CommandResult`. Mutating
tools then **auto-observe**: they fetch the resulting `UiState` and return a compact
projection, so one call is *act + observe* (the agent never has to poll separately).

| Tool | Command | Returns |
|---|---|---|
| `load_project(path, refresh?)` | `LoadProject` | ack + `app_state` (transitions to Indexing→Ready) |
| `search(query)` | `Search` | correlated `SearchCompleted` matches (name, node_kind, ids, score) |
| `activate_node(name \| id)` | `ActivateNode` | post-action `UiState` (active nodes + visible graph) |
| `activate_edge(id)` | `ActivateEdge` | post-action `UiState` |
| `activate_file(path)` | `ActivateFile` | post-action `UiState` (code view) |
| `scroll_to_line(path, line)` | `ScrollToLine` | ack + code-view position |
| `history_back()` / `history_forward()` | `HistoryBack/Forward` | post-action `UiState` |
| `create_bookmark(...)` | `CreateBookmark` | ack + bookmark id |
| `get_ui_state()` | `GetUiState` | full `UiState` projection (primary read path) |
| `get_frame(view, format)` | `GetFrame` | reassembled image (fallback / visual check) |
| `find_element(query)` | *(bridge-side)* | resolved element(s) — see below |

`find_element` is the one tool with no direct command: it runs `Search`, then ranks and
disambiguates matches (exact serialized-name > exact display name > prefix > fuzzy),
returning the best `NodeRef`(s) and their `serialized_name`. It is the semantic locator
the agent uses before `activate_node`, so the agent works in human terms ("the `parseArgs`
function") and the bridge resolves to session-stable names/ids.

### Resources (read-only pull + subscribe)

- `sourcetrail://ui-state` — the current `UiState` as JSON. Updated on relevant events;
  the bridge emits `notifications/resources/updated` to subscribers.
- `sourcetrail://frame/{view}` — latest rendered frame (image), `view` ∈ graph/code.
- `sourcetrail://project` — project path, `app_state`, error counts, indexing flag.

Resources let a passive agent read state without issuing a command; tools are for acting.

### Notifications (async app → agent)

The bridge tails `st.agent.events` and forwards:
- `AppStateChanged`, `IndexingStarted/Progress/Finished` → `notifications/resources/updated`
  on `ui-state` (+ optional MCP progress) so the agent can await indexing.
- `StatusChanged`, `ErrorCountChanged` → MCP `notifications/message` (logging).
- `NodesActivated`/`FileActivated`/`SearchCompleted` → refresh cached `ui-state`.

### Prompts (optional, later)

Templated flows, e.g. `explore_symbol(name)` = find_element → activate_node → get_ui_state.

## Correlation & event loop

Single reader loop over `st.agent.events`; a map `request_id → pending completion`.

```
tool call:
  R = next_request_id()
  send CommandEnvelope{R, cmd} on st.agent.cmd
  await CommandResult{R} on events        # ack; ok=false → MCP tool error (message)
  if mutating/read:
    R2 = next_request_id()
    send GetUiState{R2}
    await UiStateEnvelope{R2} on st.agent.state
    return project_json(UiState)
  else:
    return {ok, message}
```

- **Timeouts:** every await is bounded (e.g. 5 s ack, longer for `load_project` since it
  kicks off indexing — there, return on ack and let the agent poll `app_state`/subscribe).
- **`seq` gap detection:** `EventEnvelope.seq` is monotonic; a gap means the bridge
  missed events (late subscribe / restart) → it does a full `GetUiState` resync.
- **Cancellation race:** documented in the app side; the bridge treats a missing
  `CommandResult` within timeout as an error, not a hang.

## Data mapping

- **FlatBuffers → JSON.** The bridge decodes envelopes and re-encodes as MCP JSON content.
  `NodeRef`/`EdgeRef`/`SearchMatch`/`BookmarkInfo` → plain JSON objects.
- **Addressing.** Elements are addressed by `serialized_name` (NameHierarchy,
  session-stable) per the schema `README`; `id`s are session-local and only used as a fast
  path. `find_element`/`search` return both; tools accept either.
- **Frames.** `GetFrame` → one or more `FrameEnvelope` (chunked per ADR-0002 when a frame
  exceeds a shm segment). The bridge reassembles by `frame_seq` + `chunk_index/chunk_count`
  → `total_bytes`, then returns MCP **image content** (base64) or a resource link.

## State / FSM & error mapping

The app owns the `AppState` FSM (`NoProject/Loading/Indexing/Ready/Busy`) and already
gates commands — non-load/non-state commands while `NoProject` get
`CommandResult{ok=false, "rejected: ..."}`. The bridge surfaces these as MCP tool errors
with the message verbatim, and exposes `app_state` on every result so the agent keys its
control loop off it (e.g. wait for `Ready` after `load_project`). The bridge adds no gating
of its own — single source of truth stays in the app.

## Lifecycle & configuration

- **Launch:** Claude Code spawns the bridge as a stdio MCP server (entry in
  `.mcp.json` / client config). The bridge connects to the fixed channel names on start.
- **App not running / not connected:** channel connect fails or commands time out → tools
  return a clear "Sourcetrail not reachable (is it running with --agent-control?)" error;
  the bridge retries the connection lazily.
- **Single instance (MVP):** channel names are fixed (`st.agent.cmd`, …). Multiple app
  instances would namespace channels by an instance id passed to the bridge as an arg —
  deferred.

## Safety

- The control channel can load projects and navigate — powerful but local and read-mostly.
  `load_project` (filesystem read on the app host) is the only side-effecting tool; gate it
  behind an allowlisted root dir passed to the bridge if exposed beyond a trusted session.
- No filesystem is used for the *transport* (shared memory only), per the original goal.

## Phasing

1. **MVP:** connect + `get_ui_state`, `search`, `find_element`, `activate_node`,
   `activate_file` — proves the round-trip end-to-end against a running app (the first
   agent-driven navigation).
2. **Observe:** `ui-state` resource + `notifications/resources/updated`; indexing await.
3. **Frames:** `get_frame` + `frame/{view}` resource (visual verification).
4. **Full command set + prompts:** edges, bookmarks, history, scroll; templated flows.

## Open questions

1. ~~Rust (`rmcp`) vs. C++ (hand-rolled)~~ — **resolved: Rust (`rmcp`)**, as a Corrosion
   crate under `src/agent_mcp_bridge/`.
2. Auto-observe granularity: return full `UiState` per mutating tool, or a diff vs. the
   last snapshot to keep tool results small?
3. Do we need `ActivateEdge`/`CreateBookmark` wired app-side first (currently they fall
   through to "unknown command")? MVP tools avoid them.
4. Frame encoding/format negotiation (PNG vs. raw) and default view.

## References

- App side: `src/lib/component/controller/agent_control/` (`AgentControlController`,
  schemas, `prototype/cmd_roundtrip.cpp`).
- Contracts: `.../agent_control/schemas/*.fbs` + `schemas/README.md`.
- Umbrella design: `context/DESIGN_AGENT_UI_CONTROL.md` (Phase C).
- Chunking: `docs/adr/ADR-0002-no-shm-growth.md`.
