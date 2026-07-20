# abi-schemas

FlatBuffers schemas (`.fbs`) that define Sourcetrail's cross-process **wire
ABI**. They live at the repo root — not buried in a single library — because
they are a shared contract consumed by several independently-built components
in more than one language. Each `.fbs` is the *single source of truth*; every
consumer runs `flatc` to generate its own bindings.

## Layout

| Folder            | Contract                                   | Consumers |
|-------------------|--------------------------------------------|-----------|
| `ipc-indexer/`    | app ⇄ indexer-subprocess IPC (command queue, intermediate storage, indexing status, GC) | C++ `lib_core` (`flatc --cpp`), Rust indexer (`src/rust_indexer/indexer/build.rs`), Swift indexer (top-level `CMakeLists.txt`) |
| `ipc-mcp-agent/`  | app ⇄ MCP-bridge agent-control contract (commands, events, state, frames, snapshots) | C++ `lib_core` (`flatc --cpp`, gated on `SOURCETRAIL_AGENT_CONTROL`), Rust bridge (`src/agent_mcp_bridge/build.rs`) |

## Editing

Changing a schema changes the ABI. Every listed consumer regenerates from
these files, so a breaking edit (renaming/reordering fields, changing types)
must be coordinated across all of them — prefer additive changes (append
fields, keep IDs stable). `flatc` is located per-consumer (`$FLATC`, the
vcpkg copy, or `PATH`).

See `ipc-mcp-agent/README.md` for the agent-control message shapes.
