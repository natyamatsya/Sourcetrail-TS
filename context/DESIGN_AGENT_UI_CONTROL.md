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
  Agent (Claude) ─ MCP tools ─►  MCP bridge         Sourcetrail (QT_QPA_PLATFORM=offscreen)
                   invoke_action  (C++/Rust/Swift)   ┌────────────────────────────────────────┐
                   get_ui_state        │  thoth-ipc  │ AgentControlController                  │
                   subscribe_events    │ shared-mem  │   ├ st.agent.cmd   (route N→1) ─► dispatch Message<T>
                   get_frame           │  channels   │   ├ st.agent.events(route 1→N) ◄─ MessageListener<T>
                                       └─(no files)─►│   ├ st.agent.state (channel)  ◄─ StorageAccess
                                                     │   └ st.agent.frames(route 1→N) ◄─ QWidget::grab()
                                                     │        MessageQueue ─► Controllers/Views ─► QMainWindow
                                                     └────────────────────────────────────────┘
```

## Transport: thoth-ipc channels + typed contracts

The control plane rides `submodules/thoth-ipc` — the same high-performance
shared-memory IPC that already carries indexer results (`IpcSharedMemory`,
ADR-0002). It is a strictly better fit than a TCP/JSON socket:

- **Multiple named channels, no filesystem.** `ipc::route` (1 writer → N readers,
  pub-sub) and `ipc::channel` (N ↔ N) are named shared-memory transports
  (`submodules/thoth-ipc/cpp/libipc/include/libipc/ipc.h`). We run several in
  parallel — `st.agent.cmd`, `st.agent.events`, `st.agent.state`, `st.agent.frames`
  — each independent and lock-free. Nothing touches disk; the Phase-A
  `--screenshot <file>` becomes a bootstrap/fallback, not the real path.
- **Typed contracts, not ad-hoc JSON.** thoth-ipc's typed protocol layer uses
  **FlatBuffers** — already Sourcetrail's serialization stack
  (`External_lib_flatbuffers`, `FLATBUFFERS_GENERATED_DIR`). Define the wire as
  schemas: a `Command` union, an `Event` union, a `UiState` table, a `Frame` table.
  Schemas give versioned, evolvable, **zero-copy** messages — and because thoth-ipc
  is binary-compatible across C++/Rust/Swift, the agent side reads the exact same
  schema in any language.
- **Rendered graphics in-memory.** `QWidget::grab()` → `QImage` → encode into a
  `QByteArray` (PNG/JPEG via `QBuffer`, *not* a file) → publish a `Frame` on
  `st.agent.frames`. The agent pulls frames straight off shared memory. For
  canvas-heavy graph views this is the vision channel; for structured UI,
  `st.agent.state` suffices and frames are optional/on-demand.
- **Oversized payloads (ADR-0002).** A full-res RGBA frame (~8 MiB at 1080p) or a
  large `UiState` can exceed a fixed shm segment, and segments do **not** grow on
  macOS/Windows. Two mitigations already in the codebase's toolkit: send
  **compressed** frames (PNG/JPEG shrink 1080p to ~10²–10³ KiB — usually one
  message), and **chunk** anything still oversized via the ADR-0002 chunking pattern
  the indexer already uses.
- **Optional AEAD codec** (thoth-ipc secure envelope) if the control plane needs to
  be authenticated.

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

A new controller (debug/flag-gated), modelled on `IDECommunicationController` but
backed by the thoth-ipc channels above instead of the plugin's TCP/`NetworkProtocolHelper`
framing, and speaking FlatBuffers contracts instead of JSON. It consumes `st.agent.cmd`
and is a `MessageListener<...>` that republishes observed changes onto `st.agent.events`.
Responsibilities:

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
- **`get_ui_state`** → publish a `UiState` FlatBuffer on `st.agent.state`: current
  project path (`Application::getCurrentProjectPath`), active tokens as name
  hierarchies, the visible graph (`StorageAccess::getGraphForActiveTokenIds`), current
  file + scroll (CodeController), last search matches (SearchController), open tabs,
  bookmarks. This is the deterministic "what's on screen" the agent reads instead of
  screenshotting.
- **`find_element`** → resolve a human name to an actionable id via
  `StorageAccess::getNodeIdForNameHierarchy` / `getAutocompletionMatches`, so
  `activate_node` can target it.
- **`get_frame`** → `QWidget::grab()` → encode → publish a `Frame` on
  `st.agent.frames` (in-memory, no file). The vision channel for the canvas-heavy graph
  view the structured state can't fully describe.
- **`subscribe_events`** → the `MessageListener` republishes each observed change as an
  `Event` on `st.agent.events`, so the agent can react to activations / indexing
  progress without polling.

Gate it behind a compile flag (`SOURCETRAIL_AGENT_CONTROL`) and/or the CLI flag so it
never ships in release input paths. It doubles as a **subcutaneous test harness**.

### 3. MCP server

A small external MCP bridge that connects to the thoth-ipc channels via the
**C++/Rust/Swift** binding (same binary wire format) and exposes typed tools:
`invoke_action`, `get_ui_state`, `find_element`, `get_frame`, `subscribe_events`.
Because the contracts are FlatBuffers, the bridge parses zero-copy and stays
language-agnostic — no socket, no file. The agent gets a fast deterministic path
(commands + `UiState`) for ~90% and the `frames` channel only for the rest.

## Command semantics: acknowledgements + application state

The message bus is already a command pattern — encapsulated, queued, with undo/redo
(`UndoRedoController`) and replay flags on `MessageBase`; the FlatBuffers `Command`
union is that pattern made typed + serializable. A remote-control protocol needs two
things on top, added here (not a parallel C++ command hierarchy):

- **Closed-loop acks.** Every command emits a `CommandResult` (on `st.agent.events`,
  correlated by `request_id`): `ok`, or `ok=false` with a message. `GetUiState` also
  replies with the `UiStateEnvelope` on `st.agent.state`. The agent's loop is thus
  *issue command → await `CommandResult`*, not fire-and-forget.
- **An explicit application FSM.** `AppState`
  (`NoProject · Loading · Indexing · Ready · Busy`) is derived from Sourcetrail's
  existing `Project::ProjectStateType` + `isIndexing()` — surfaced on
  `UiState.app_state` and via the `AppStateChanged` event. This makes the agent
  deterministic: *read state → issue a valid command → await the transition*. It
  reuses the project state rather than inventing a parallel model.
- **State-gated commands.** A command illegal in the current state is rejected via
  `CommandResult` (`ok=false`, message `"rejected: ..."`) — e.g. `search` while
  `NoProject`. The FSM is what makes gating possible.

## Log event forwarding (WARN/ERROR)

The `events` channel carries *domain* events (status, error counts, indexing,
app-state). It deliberately does **not** carry the raw diagnostic log — that stream
(`LOG_INFO` config warnings, etc.) is a firehose whose high-value signals are *already*
domain events. The one thing an agent otherwise can't see is the detailed text behind a
failure: `CommandResult` gives a terse `"rejected: ..."`, while the log holds the "why"
(`"could not load project: ... wrong file ending"`). So forward **warnings and errors
only** as a distinct event; leave INFO/debug in the file log.

### Schema — `LogEvent` (a new `Event` arm)

```fbs
enum LogLevel : byte { Info = 0, Warning = 1, Error = 2 }   // Info only if widened (SetLogLevel)
table LogEvent {
    level:    LogLevel;
    message:  string;
    // Origin, from LogMessage — lets an agent attribute/dedupe without parsing text:
    file:     string;    // LogMessage::getFileName()
    function: string;
    line:     uint32;
    logger:   string;    // Logger::getType() of the emitting sink
}
```

Appended to `union Event { … , LogEvent }` (append-only: union tags are positional, so a
new arm at the end is wire-compatible) and rides the existing `EventEnvelope` (`seq` +
`timestamp_ms`) — late subscribers keep gap detection, and the bridge's `parse_event`
gains one arm.

### Producer — `AgentLogger : Logger`

`Logger` already has the machinery: a `LogLevelMask` with
`setLogLevel(LOG_WARNINGS | LOG_ERRORS)` (INFO filtered for free) and the
`logWarning`/`logError(LogMessage)` overrides. Register one via
`LogManager::addLogger(...)` on `startListening()`, remove it on stop. `LogMessage`
supplies everything the schema needs (`message`, `getFileName()`, `functionName`,
`line`, `time`, `threadId`).

### Threading — the one real constraint

`LOG_*` fires from **any** thread (indexer pool, GUI, message thread), but
`st.agent.events` is written only from the message-processing thread. So `AgentLogger`
must not touch the route directly: it enqueues each record onto a mutex'd/lock-free
queue, and the controller drains it on its own thread (the one that already publishes
every other event via `publishEvent`) and emits the `LogEvent`s there. All route writes
stay single-threaded (no new route synchronization), and a `LOG_*` emitted *while*
publishing a `LogEvent` just queues — it can't recurse into the route.

### Gating — free when unwatched, bounded when watched

- **Connected-only.** Skip the encode+publish unless `events.recv_count() > 0`; with no
  agent attached the drain is a no-op, so logging stays free.
- **Level.** Default `Warning`+above; adjustable at runtime via the `SetLogLevel` command
  (below) — a knob, not a rebuild.
- **Backpressure.** The broadcast ring is bounded (256 elements). Cap the drain per tick
  and coalesce consecutive duplicates (`"…" ×N`) so a log storm can't starve domain
  events; drop-with-a-counter beats blocking the message thread.

### `SetLogLevel` command

```fbs
table SetLogLevel { min_level: LogLevel = Warning; }   // forward this level and above
```

A new `Command` arm (append-only). **Threshold** semantics, not a raw bitmask: `min_level`
maps to the `Logger::LogLevelMask` the controller sets on its `AgentLogger` —
`Error → LOG_ERRORS`, `Warning → LOG_WARNINGS|LOG_ERRORS`, `Info → LOG_ALL`. Handled
inline in the controller (it owns the logger), *not* dispatched as a `Message`; it acks
via `CommandResult` like any other command. Widening to `Info` is how an agent turns the
firehose on for one debugging session; the controller resets to `Warning` on
`startListening()` so a session never inherits a previous agent's verbosity.

### Bridge

`parse_event` gains a `LogEvent` arm → `Incoming::Log { level, message, … }`. The MCP
server surfaces it as a notification/`logs` resource an agent subscribes to rather than
polls; the smoke client can print them inline.

### Phasing

Self-contained — schema arm + `AgentLogger` + drain + one command; no dependency on the
frame or snapshot work. Fits **Phase D** (broadening the event/command vocabulary). Ship
the `Warning`+above slice first; `SetLogLevel` widening and coalescing are follow-ons.

## Per-element screenshots (`CaptureElement`)

`GetFrame` grabs the whole main window (or the graph canvas); `get_snapshot` gives the
structural tree, where every node already carries an `ElementRef` *and* its `rect`. The
missing piece is a *visual* view of one element — "show me just this button / cell /
graph node". Add a command that screenshots a single element by its `ElementRef`:

```fbs
table CaptureElement {
    ref:                ElementRef;
    format:             FrameFormat = Png;
    include_properties: bool = false;   // also reply with the element's UiNode (strings + props)
}  // -> FrameEnvelope on st.agent.frames  (+ a single-node UiSnapshot on st.agent.snapshot)
```

A new `Command` arm (append-only). The reply reuses the **frames channel** and
`FrameEnvelope` — an element capture and a whole-window frame are the same kind of
payload, so they share one transport and its ADR-0002 chunking. Two append-only fields on
`FrameEnvelope` let it correlate and self-describe (both wire-compatible additions):

```fbs
// appended to FrameEnvelope:
request_id: uint64;      // 0 for a GetFrame stream; the command id for CaptureElement
element:    ElementRef;  // echoed for CaptureElement; empty for whole-window frames
```

`width`/`height` become the element's pixel size; `view` stays the containing view.

### Also the strings, not just pixels

Yes — an element's **UI-visible strings come back too**, and they need no new type. A
`UiNode` already carries `name`/`value`/`description` (straight from
`QAccessible::Name`/`Value`/`Description`) plus a `properties` FlexBuffer bag of
Q_PROPERTYs (`text`, `toolTip`, `displayText`, `currentText`, …). So with
`include_properties`, `CaptureElement` *additionally* replies with a single-node
`UiSnapshot` — the target as a leaf `UiNode`, no children — on `st.agent.snapshot`,
correlated by `request_id`. It is literally `GetSnapshot` scoped to one element: from one
command the agent gets the picture on `frames` and the strings/props on `snapshot`. (If
you find yourself wanting the element's *subtree* text too, that's the cue to add a
`root_ref` scope to `GetSnapshot` rather than growing `CaptureElement`.)

### Resolving a ref to pixels

`CaptureElement` must turn an `ElementRef` back into a live widget — via the shared
`QtUiControl::resolve` helper (see *Structural control* below; `InvokeAction` uses the
same one): walk the QAccessible/QObject tree `QtUiSnapshot` builds and match by
`object_name` + `path` (role/name/index). Then:

- The element *is* / backs a `QWidget` (`QAccessibleInterface::object()` → `QWidget`):
  `widget->grab()`.
- A sub-element with no `QWidget` of its own (menu item, table cell, graph node): grab the
  containing top-level widget and crop to the element's `QAccessibleInterface::rect()`
  mapped into that widget's coordinates; for `QGraphicsItem`s, render the scene region.

Capture runs on the **GUI thread** — the same `ui()` hop the snapshot already uses
(`grab()`/`render()` are GUI-only) — then encode (`QPixmap` → PNG via `QBuffer`), chunk,
and publish the `FrameEnvelope`(s), after a `CommandResult` ack. Failure modes ack
`ok=false`: element not found, not visible, or zero-size rect. Offscreen is fine —
`grab()` works under `QT_QPA_PLATFORM=offscreen` (same path as `--screenshot`).

This closes the loop with the snapshot: **get_snapshot → pick an element (ref + actions +
rect) → `CaptureElement(ref)` to see it, or `InvokeAction(ref)` to act on it.** Fits
**Phase D** alongside the snapshot/structural-control work.

## Structural control (`InvokeAction`)

The snapshot is the *reader*; `InvokeAction` is the symmetric *sender* — "read what you
can do (`UiNode.actions`), then do it" on an element addressed in that same tree. The full
rationale and prior-art (GammaRay) live in
[`DESIGN_AGENT_UI_SNAPSHOT.md`](DESIGN_AGENT_UI_SNAPSHOT.md); the **command contract lives
here**, with the other commands.

### The shared resolver — `QtUiControl`

`InvokeAction` and `CaptureElement` both need to turn an `ElementRef` back into a live
target, so both use one helper: `QtUiControl` (sibling of `QtUiSnapshot` in `lib_gui`).
`resolve(ElementRef)` re-walks from `QApplication::topLevelWidgets()`, matching the
`object_name` anchor + the `path` of `{role, name, index}` steps, down to the
`QAccessibleInterface` / `QObject` / `QWidget`. A snapshot is point-in-time, so a ref is a
**re-resolvable selector, never a pointer**: role+name paths survive unrelated reordering,
and the `objectName` anchor (the Phase-D hygiene pass) makes them rock-solid. Runs on the
GUI `ui()` hop, like the snapshot and the element capture.

### Command contract

```fbs
table InvokeAction   { target: ElementRef; action: string; text: string; }   // primary
table InvokeMethod   { target: ElementRef; method: string; args: [string]; }  // gated
table SendInputEvent { target: ElementRef; kind: InputKind; x: int32; y: int32; key: string; } // gated
```

Append-only `Command` arms. Each acks via `CommandResult` and — act-and-observe — the
agent follows with `get_ui_state`/`get_snapshot` (or a diff) to see the effect. Resolution
failures are *data*, not transport errors: `ok=false`, `"element not found: <ref>"` /
`"action not supported"`. `InvokeAction.text` sets the element's value where the action is
a value edit (line edits, spin/combo) via `QAccessibleValueInterface` /
`QAccessibleEditableTextInterface`; empty otherwise.

### Three mechanisms, mirroring the three snapshot sources

| Snapshot exposes | Sender uses | For |
|---|---|---|
| `actions` (QAccessibleActionInterface) | **`doAction(name)`** | buttons, checkboxes, menu items, list/tree rows, combos — normalized; works for model/view items + QML |
| invokable methods (QMetaObject) | **`QMetaObject::invokeMethod`** | app-specific slots/`Q_INVOKABLE` with no accessible action |
| geometry (`rect`) | **synthesized `QMouseEvent`/`QKeyEvent`** at a point | custom-painted widgets and the `QGraphicsScene` graph (items aren't QObjects) |

Preference is top-to-bottom: the accessible action is the safe, normalized default (the
snapshot already told the agent which names are valid); method invocation and synthesized
events are escape hatches for the long tail.

### Three tiers (this doesn't replace the semantic commands)

- **Tier 1 — semantic `Command`s** (`ActivateNode`, `LoadProject`, `ActivateTab`, …):
  curated, robust, mapped to `Message<T>` dispatches. The default and preferred path.
- **Tier 2 — structural `InvokeAction`**: generic, accessibility-normalized; the long tail
  of UI the semantic commands don't cover.
- **Tier 3 — `InvokeMethod` / `SendInputEvent`**: powerful escape hatches (arbitrary slot
  invocation, synthetic input) — **capability-gated and logged**, off unless explicitly
  enabled for a trusted session.

## Why not lean on Layers 2/3 here

- **Accessibility tree (AT-SPI/UIA/AX):** would require setting `objectName`s +
  `QAccessible` metadata across the widget tree and still only exposes *widgets*, not
  Sourcetrail's domain (nodes, edges, symbols, trails). The message bus already speaks
  that domain. Worth doing the `objectName`/`QAccessible` hygiene anyway (it helps real
  screen-reader users and any future vision/accessibility agent), but it is not the
  primary seam.
- **Vision:** kept only as the `st.agent.frames` channel fallback for the graph canvas.

## Phased plan

- **Phase A — headless GUI.** Add the `--agent-control` CLI path that runs the GUI branch
  under `QT_QPA_PLATFORM=offscreen`. Verify: a project loads, the message loop runs, and
  `QMainWindow::grab()` produces a PNG, all with no display. No new protocol yet.
- **Phase B — control channels.** Add `AgentControlController` + the thoth-ipc
  `cmd`/`events`/`state` channels with FlatBuffers `Command`/`Event`/`UiState` schemas
  and a first command set (`load_project`, `search`, `activate_node`, `activate_file`).
  Prove round-trips (send a `Command`, read back a `UiState`) against a small indexed
  project — no filesystem.
- **Phase C — frames + MCP bridge.** Add the `st.agent.frames` channel
  (`grab()` → encode → publish, with ADR-0002 chunking) and wrap the channels in an MCP
  bridge exposing the tools + `find_element`. First end-to-end agent-driven session.
- **Phase D — broaden.** Grow the command union and `UiState` schema; add the
  `objectName`/`QAccessible` hygiene pass; add the **structural UI snapshot** (Qt
  meta-object / accessibility tree → JSON, so the agent can navigate *any* widget/action,
  not just curated commands) — see [`DESIGN_AGENT_UI_SNAPSHOT.md`](DESIGN_AGENT_UI_SNAPSHOT.md);
  add a golden-state test mode that replays a command script and diffs `UiState`
  (regression harness — also de-risks the stdexec migration, see
  `ROADMAP_STDEXEC_MIGRATION.md`).

Each phase is independently useful and testable; Phase A alone unlocks screenshot-based
verification of UI changes in CI.

## Key seams (file references)

| Concern | Symbol | File |
|---|---|---|
| Dispatch a command | `Message<T>::dispatch()` | `src/lib/utility/messaging/Message.h:21-31` |
| Command queue | `MessageQueue` | `src/lib/utility/messaging/MessageQueue.h` |
| Observe state | `MessageListener<T>` | `src/lib/utility/messaging/MessageListener.h:10-35` |
| Command vocabulary | 91 `Message*` types | `src/lib/utility/messaging/type/` |
| Transport (control plane) | `ipc::route` / `ipc::channel` | `submodules/thoth-ipc/cpp/libipc/include/libipc/ipc.h` |
| shm usage precedent | `IpcSharedMemory` | `src/lib/utility/interprocess/` |
| Typed contracts | FlatBuffers | `External_lib_flatbuffers`, `FLATBUFFERS_GENERATED_DIR` |
| Oversized-payload chunking | ADR-0002 | `docs/adr/ADR-0002-no-shm-growth.md` |
| Existing external control (precedent) | `IDECommunicationController` | `src/lib/component/controller/IDECommunicationController.h` |
| GUI vs headless branch | `main()` | `src/app/main.cpp:130-215` |
| GUI bootstrap | `Application::createInstance` | `src/lib/Application.cpp:36-83` |
| View creation | `QtViewFactory` | `src/lib_gui/qt/view/QtViewFactory.h` |
| Main window (grab target) | `QtMainView` / `QtMainWindow` | `src/lib_gui/qt/view/QtMainView.h:17-77` |
| State to serialize | `StorageAccess` | `src/lib/data/storage/StorageAccess.h` |

## Open questions to resolve before Phase B

1. **Transport — resolved:** thoth-ipc (`ipc::route`/`ipc::channel`) with FlatBuffers
   contracts, over the existing shared-memory stack. No TCP socket, no filesystem. Reuse
   the app's `IpcSharedMemory` wrapper vs. call `libipc` directly for the agent channels?
   (Lean: a thin `AgentChannels` wrapper mirroring `IpcSharedMemory`.)
2. **Channel topology:** one `channel` (N↔N) for request/response `cmd`+`state` with
   correlation ids, or separate `route`s per direction (`cmd` N→1, `events`/`state`/
   `frames` 1→N)? (Lean: separate routes — clean unidirectional flows, natural pub-sub
   for multiple agent subscribers.)
3. **Gating:** compile flag (`SOURCETRAIL_AGENT_CONTROL`) vs pure runtime CLI flag.
   Recommend compile-flag-gated so it is absent from release binaries, plus the CLI flag
   to activate it in debug builds.
4. **Synchrony:** a `get_ui_state` after an `invoke_action` must observe the *settled*
   state. The message loop is async (`MessageQueue::startMessageLoop`), so the controller
   needs a "quiesce" barrier (dispatch, wait for the queue to drain, then snapshot) —
   likely a `MessageFinishedProcessing`-style signal or a queue-idle wait.
5. **Frames:** compressed (PNG/JPEG) vs raw RGBA; on-demand (`get_frame`) vs a streamed
   `route`; and the `frames` segment size + ADR-0002 chunk threshold.
6. **Command coverage & argument encoding:** which of the 91 messages to expose first
   (start with the table above); prefer human-readable name hierarchies in the `Command`
   schema, resolved to ids server-side.
