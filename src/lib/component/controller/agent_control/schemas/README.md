# Agent-control FlatBuffers contracts

Wire schemas for driving Sourcetrail's UI from an AI agent over thoth-ipc
shared-memory channels — no filesystem. Design:
[`context/DESIGN_AGENT_UI_CONTROL.md`](../../../../../../context/DESIGN_AGENT_UI_CONTROL.md).

Namespace: `Sourcetrail.Agent`. One `root_type` per channel; all include
`agent_common.fbs`.

| Channel (thoth-ipc) | Kind | Direction | Schema | Root |
|---|---|---|---|---|
| `st.agent.cmd` | `ipc::route` N→1 | agent → app | `agent_command.fbs` | `CommandEnvelope` |
| `st.agent.events` | `ipc::route` 1→N | app → agents | `agent_event.fbs` | `EventEnvelope` |
| `st.agent.state` | `ipc::channel` | reply to `GetUiState` | `agent_state.fbs` | `UiStateEnvelope` |
| `st.agent.frames` | `ipc::route` 1→N | app → agents | `agent_frame.fbs` | `FrameEnvelope` |
| _(shared types)_ | — | — | `agent_common.fbs` | — |

- **`Command`** is a union mapping 1:1 onto `Message<T>` dispatches (comments name
  the target message). `CommandEnvelope.request_id` correlates async replies.
- **`Event`** republishes message-bus changes (the controller is a
  `MessageListener`). `seq` lets a late subscriber detect gaps.
- **`UiState`** is the deterministic "what's on screen", assembled from
  `StorageAccess` + controllers — the primary read path; frames are the fallback.
- **`FrameEnvelope`** carries an encoded frame (chunked per ADR-0002 when it
  exceeds a shm segment; single chunk otherwise).

Elements are addressed by `serialized_name` (NameHierarchy, session-stable); the
controller resolves names to database ids.

## Validate / generate

```sh
flatc --cpp -I . -o <out> agent_common.fbs agent_command.fbs \
      agent_event.fbs agent_state.fbs agent_frame.fbs
```

Validated with flatc 25.9.23 (round-trip verified). CMake generation into
`FLATBUFFERS_GENERATED_DIR` is wired in Phase B, when `AgentControlController`
first consumes the headers (mirrors `data/indexer/interprocess/schemas/`).
