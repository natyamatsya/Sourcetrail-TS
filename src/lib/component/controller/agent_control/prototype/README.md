# st.agent.cmd round-trip prototype

Proves the Phase-B transport for agent-UI control (see
[`context/DESIGN_AGENT_UI_CONTROL.md`](../../../../../../context/DESIGN_AGENT_UI_CONTROL.md)):
the real thoth-ipc `ipc::route` channels carrying the FlatBuffers contracts from
[`../schemas`](../schemas), with **no filesystem**.

Flow: the *agent* builds a `CommandEnvelope(GetUiState, request_id=7)` and sends it
on `st.agent.cmd`; the *app* verifies + decodes the union, "dispatches", and replies
a `UiStateEnvelope` on `st.agent.state`; the agent verifies the correlation id and
payload.

- `cmd_roundtrip.cpp` — agent + app as two threads (both sides log).
- `cmd_roundtrip_fork.cpp` — agent + app as two **separate processes** (`fork`), the
  real deployment; shared memory only.

Standalone reference, not wired into the build. Phase B turns this into the
`AgentControlController` + generated headers proper.

## Build & run (verified)

Requires the thoth-ipc C++ lib built once (`submodules/thoth-ipc/cpp/libipc/build/lib/libipc.a`)
and `flatc` + FlatBuffers headers (both come with the vcpkg toolchain).

```sh
REPO=$(git rev-parse --show-toplevel)
IPC=$REPO/submodules/thoth-ipc/cpp/libipc
FBINC=$REPO/.build/dbg/vcpkg_installed/arm64-osx/include          # flatbuffers headers
FLATC=$REPO/.build/dbg/vcpkg_installed/arm64-osx/tools/flatbuffers/flatc

# 1. generate the contract headers
mkdir -p gen && "$FLATC" --cpp -I ../schemas -o gen ../schemas/*.fbs

# 2. build + run (swap in cmd_roundtrip_fork.cpp for the cross-process variant)
c++ -std=gnu++23 -I "$IPC/include" -I gen -isystem "$FBINC" \
    cmd_roundtrip.cpp "$IPC/build/lib/libipc.a" -o cmd_roundtrip
./cmd_roundtrip
```

Expected:

```
app: recv CommandEnvelope request_id=7 command=GetUiState
agent: recv UiStateEnvelope request_id=7 project='/tmp/demo.srctrl.toml'
CMD ROUND-TRIP PASS: FlatBuffers CommandEnvelope -> UiStateEnvelope over thoth-ipc shm
```

Verified on macOS/arm64 with thoth-ipc `libipc`, flatc 25.9.23, LLVM clang, C++23 —
both the two-thread and the cross-process (`fork`) variants pass.
