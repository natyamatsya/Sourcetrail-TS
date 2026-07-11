# Agent-driven UI control — design for Sourcetrail

**Goal:** let an AI agent drive Sourcetrail's GUI headlessly — load a project,
search, activate nodes/edges, navigate code, read back "what's on screen" — as
development and testing support, and so the agent can verify UI-facing changes
end to end.

This is the Sourcetrail-specific design. It builds on the general framework in
[`ai-agent-headless-ui-exploration.md`](ai-agent-headless-ui-exploration.md)
(the three-layer model: semantic seam → accessibility tree → vision). That brief
ends by asking for the stack; this doc answers it and turns the recommendation
into concrete work against the real codebase.

## The stack (answered)

- **UI toolkit:** Qt6 **Widgets** (not QML). GUI code is isolated in `Sourcetrail_lib_gui`
  (`src/lib_gui/`); core logic and the abstract View/Controller layer are in
  `Sourcetrail_lib` (`src/lib/`).
- **Target:** cross-platform (CI matrix: ubuntu / macos / windows), primary dev on macOS.
- **Architecture:** message-bus MVC. Every UI action and state change flows through a
  central `MessageQueue`, decoupled from the Qt widgets.

## The key insight: the semantic seam already exists

The exploration doc's highest-leverage recommendation — *"structure the app so logic
lives behind a scriptable command bus, then expose that seam"* — is **already built**
in Sourcetrail. We do not need to build an MVVM seam; we need to **expose the one
that exists**:

- **Command bus:** `MessageQueue` (`src/lib/utility/messaging/MessageQueue.h`) with
  **91 `Message*` types** (`src/lib/utility/messaging/type/`). Any action is
  `Message<T>(...).dispatch()` (queued) or `.dispatchImmediately()`
  (`src/lib/utility/messaging/Message.h:21-31`). This is the agent's write path.
- **Observation:** `MessageListener<T>` auto-registers and receives every message of
  a type (`src/lib/utility/messaging/MessageListener.h:10-35`). Controllers already use
  it; an agent listener can observe *all* state changes broadcast as messages.
- **State:** `StorageAccess` / `StorageCache` (`src/lib/data/storage/StorageAccess.h`)
  answers "what would be on screen" — the active graph, search matches, source
  locations, name hierarchies — without touching a widget.
- **Widgets are a thin skin:** `Component` = abstract `View` + `Controller`
  (`src/lib/component/Component.h`); Qt implementations are behind a `ViewFactory`
  (`src/lib_gui/qt/view/QtViewFactory.h`). Because we drive via **messages, not
  synthetic input**, widget focus/coordinates are irrelevant — which sidesteps the
  offscreen-focus caveat the exploration doc flags for input-based automation.

There is even a **precedent control channel**: the editor-plugin IPC
(`IDECommunicationController` + `QtIDECommunicationController` over a TCP socket,
wire format in `NetworkProtocolHelper.h`) already lets an *external* process inject
`SET_ACTIVE_TOKEN` / `CREATE_PROJECT` / `PING` and receive cursor moves. It proves the
pattern; it is just too narrow (4 message types, plugin-shaped) to reuse directly.

## Architecture

```
   Agent (Claude)                      Sourcetrail process (QT_QPA_PLATFORM=offscreen)
   ┌────────────┐   MCP tools    ┌───────────────────────────────────────────────┐
   │  MCP client│ ─────────────► │  MCP server ◄─JSON/socket─► AgentControlController│
   └────────────┘  invoke_action │                              │        │          │
                    get_ui_state │            dispatch Message<T>│        │StorageAccess
                    find_element │                               ▼        ▼          │
                    screenshot   │            MessageQueue ─► Controllers/Views ──► QMainWindow
                                 └───────────────────────────────────────────────┘
```

Three pieces, smallest-first:

### 1. Run the real GUI headlessly

Use the **GUI branch** of `main.cpp` (`QtApplication` + `QtViewFactory` +
`QtNetworkFactory`, `src/app/main.cpp:176-215`) under `QT_QPA_PLATFORM=offscreen`,
**not** the existing `--without-gui` indexing branch (which passes `nullptr`
factories, so no views/controllers exist — `main.cpp:130-175`). We want the full
view/controller graph alive so state is real; we just don't want a visible window.

Offscreen renders with no display, so `QWidget::grab()` still yields a pixmap for the
screenshot fallback. Add a CLI flag (e.g. `--agent-control[=<port>]`) that selects the
GUI path, forces the offscreen platform if unset, and starts the control channel.

### 2. `AgentControlController` — the exposed seam

A new controller (debug/flag-gated), modelled on `IDECommunicationController` but with
a JSON protocol and state queries. It is both a `MessageListener<...>` (to observe) and
a small local socket server (to receive commands). Responsibilities:

- **`invoke_action`** → construct and `dispatch()` a `Message*`. Back it with a
  **command registry**: `name -> factory(json args)`. Start with a curated high-value
  set, not all 91 types:
  | command | message |
  |---|---|
  | `load_project` | `MessageLoadProject` |
  | `search` / `find` | `MessageSearch` / `MessageFind` |
  | `activate_node` | `MessageActivateNodes` (resolve name → id first) |
  | `activate_edge` | `MessageActivateEdge` |
  | `activate_file` | `MessageActivateFile` |
  | `scroll_to_line` | `MessageScrollToLine` |
  | `back` / `forward` | `MessageHistoryUndo` / `MessageHistoryRedo` |
  | `bookmark_*` | `MessageBookmarkCreate/Activate/...` |
- **`get_ui_state`** → serialize a snapshot as JSON: current project path
  (`Application::getCurrentProjectPath`), active tokens as name hierarchies, the visible
  graph (`StorageAccess::getGraphForActiveTokenIds`), current file + scroll
  (CodeController), last search matches (SearchController), open tabs, bookmarks. This is
  the deterministic "what's on screen" the agent reads instead of screenshotting.
- **`find_element`** → resolve a human name to an actionable id via
  `StorageAccess::getNodeIdForNameHierarchy` / `getAutocompletionMatches`, so
  `activate_node` can target it.
- **`screenshot`** → `QMainWindow::grab()` → PNG (vision fallback for the
  canvas-heavy graph view the message state can't fully describe).

Gate it behind a compile flag (`SOURCETRAIL_AGENT_CONTROL`) and/or the CLI flag so it
never ships in release input paths. It doubles as a **subcutaneous test harness**.

### 3. MCP server

A small external MCP server (Python/TS, matching the `kwin-mcp` / Windows-365 templates)
that connects to the control socket and exposes typed tools: `invoke_action`,
`get_ui_state`, `find_element`, `screenshot`. External process = language-agnostic and
keeps Qt/MCP dependencies out of the app. The agent gets a fast deterministic path
(messages + state JSON) for ~90% and vision only for the rest.

## Why not lean on Layers 2/3 here

- **Accessibility tree (AT-SPI/UIA/AX):** would require setting `objectName`s +
  `QAccessible` metadata across the widget tree and still only exposes *widgets*, not
  Sourcetrail's domain (nodes, edges, symbols, trails). The message bus already speaks
  that domain. Worth doing the `objectName`/`QAccessible` hygiene anyway (it helps real
  screen-reader users and any future vision/accessibility agent), but it is not the
  primary seam.
- **Vision:** kept only as the `screenshot` fallback for the graph canvas.

## Phased plan

- **Phase A — headless GUI.** Add the `--agent-control` CLI path that runs the GUI branch
  under `QT_QPA_PLATFORM=offscreen`. Verify: a project loads, the message loop runs, and
  `QMainWindow::grab()` produces a PNG, all with no display. No new protocol yet.
- **Phase B — control channel.** Add `AgentControlController` + a local JSON socket with a
  first command set (`load_project`, `search`, `activate_node`, `activate_file`) and
  `get_ui_state`. Prove round-trips against a small indexed project.
- **Phase C — MCP server.** Wrap the channel in an MCP server exposing the four tools;
  wire `find_element` and `screenshot`. First end-to-end agent-driven session.
- **Phase D — broaden.** Grow the command registry and the state schema; add the
  `objectName`/`QAccessible` hygiene pass; add a golden-state test mode that replays a
  message script and diffs `get_ui_state` (regression harness).

Each phase is independently useful and testable; Phase A alone unlocks screenshot-based
verification of UI changes in CI.

## Key seams (file references)

| Concern | Symbol | File |
|---|---|---|
| Dispatch a command | `Message<T>::dispatch()` | `src/lib/utility/messaging/Message.h:21-31` |
| Command queue | `MessageQueue` | `src/lib/utility/messaging/MessageQueue.h` |
| Observe state | `MessageListener<T>` | `src/lib/utility/messaging/MessageListener.h:10-35` |
| Command vocabulary | 91 `Message*` types | `src/lib/utility/messaging/type/` |
| Existing external control | `IDECommunicationController` | `src/lib/component/controller/IDECommunicationController.h` |
| IPC wire format (precedent) | `NetworkProtocolHelper` | `src/lib/component/controller/helper/NetworkProtocolHelper.h` |
| GUI vs headless branch | `main()` | `src/app/main.cpp:130-215` |
| GUI bootstrap | `Application::createInstance` | `src/lib/Application.cpp:36-83` |
| View creation | `QtViewFactory` | `src/lib_gui/qt/view/QtViewFactory.h` |
| Main window (grab target) | `QtMainView` / `QtMainWindow` | `src/lib_gui/qt/view/QtMainView.h:17-77` |
| State to serialize | `StorageAccess` | `src/lib/data/storage/StorageAccess.h` |

## Open questions to resolve before Phase B

1. **Protocol:** newline-delimited JSON over a local TCP/unix socket (simple, matches the
   existing TCP IPC), or reuse the plugin `NetworkProtocolHelper` framing? Recommend fresh
   JSON — the plugin format is too narrow.
2. **Gating:** compile flag (`SOURCETRAIL_AGENT_CONTROL`) vs pure runtime CLI flag.
   Recommend compile-flag-gated so it is absent from release binaries, plus the CLI flag
   to activate it in debug builds.
3. **Synchrony:** `get_ui_state` after an `invoke_action` must observe the *settled* state.
   The message loop is async (`MessageQueue::startMessageLoop`), so the channel needs a
   "quiesce" barrier (dispatch, wait for the queue to drain, then snapshot) — likely a
   dedicated `MessageFinishedProcessing`-style signal or a queue-idle wait.
4. **Command coverage:** which of the 91 messages to expose first (start with the table
   above) and how to express their arguments in JSON (ids vs name hierarchies — prefer
   human-readable name hierarchies, resolved server-side).
