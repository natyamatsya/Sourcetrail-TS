// Prototype: st.agent.cmd round-trip over thoth-ipc with the real FlatBuffers
// contracts. Agent sends a CommandEnvelope(GetUiState) on st.agent.cmd; the app
// side decodes it, "dispatches", and replies a UiStateEnvelope on st.agent.state;
// the agent verifies. No filesystem — pure shared memory.
#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include <libipc/ipc.h>
#include <flatbuffers/flatbuffers.h>

#include "agent_command_generated.h"
#include "agent_state_generated.h"

using namespace Sourcetrail::Agent;
static constexpr uint64_t kReqId = 7;
static const char* kProject = "/tmp/demo.srctrl.toml";

static void appSide(std::atomic<int>& ok) {
  ipc::route cmd{"st.agent.cmd", ipc::receiver};     // agent -> app
  ipc::route state{"st.agent.state", ipc::sender};   // app -> agent
  ipc::buff_t in;
  for (int i = 0; i < 50 && in.empty(); ++i) in = cmd.recv(200);
  if (in.empty()) { std::printf("app: no command received\n"); ok = -1; return; }

  flatbuffers::Verifier v(static_cast<const uint8_t*>(in.data()), in.size());
  if (!VerifyCommandEnvelopeBuffer(v)) { std::printf("app: bad CommandEnvelope\n"); ok = -1; return; }
  const auto* env = GetCommandEnvelope(in.data());
  const uint64_t reqId = env->request_id();
  const bool isGetState = env->command_type() == Command_GetUiState;
  std::printf("app: recv CommandEnvelope request_id=%llu command=%s\n",
              (unsigned long long)reqId, EnumNameCommand(env->command_type()));

  // "dispatch" -> assemble a UiState reply
  flatbuffers::FlatBufferBuilder b;
  auto name = b.CreateString("app::Foo");
  std::vector<flatbuffers::Offset<NodeRef>> nodes{ CreateNodeRef(b, 1, name, 8, 0) };
  auto ui = CreateUiState(b, b.CreateString(kProject), b.CreateVector(nodes));
  b.Finish(CreateUiStateEnvelope(b, reqId, ui));
  state.wait_for_recv(1, 2000);                       // ensure agent's receiver is up
  state.send(b.GetBufferPointer(), b.GetSize());
  ok = isGetState ? 1 : -1;
}

static void agentSide(std::atomic<int>& ok) {
  ipc::route cmd{"st.agent.cmd", ipc::sender};        // agent -> app
  ipc::route state{"st.agent.state", ipc::receiver};  // app -> agent (connect before sending)

  flatbuffers::FlatBufferBuilder b;
  auto gus = CreateGetUiState(b);
  b.Finish(CreateCommandEnvelope(b, kReqId, Command_GetUiState, gus.Union()));
  cmd.wait_for_recv(1, 2000);                          // ensure app's receiver is up
  cmd.send(b.GetBufferPointer(), b.GetSize());

  ipc::buff_t in;
  for (int i = 0; i < 50 && in.empty(); ++i) in = state.recv(200);
  if (in.empty()) { std::printf("agent: no reply received\n"); ok = -1; return; }
  flatbuffers::Verifier v(static_cast<const uint8_t*>(in.data()), in.size());
  if (!VerifyUiStateEnvelopeBuffer(v)) { std::printf("agent: bad UiStateEnvelope\n"); ok = -1; return; }
  const auto* env = GetUiStateEnvelope(in.data());
  const std::string proj = env->state() && env->state()->project_file_path()
                             ? env->state()->project_file_path()->str() : "";
  std::printf("agent: recv UiStateEnvelope request_id=%llu project='%s'\n",
              (unsigned long long)env->request_id(), proj.c_str());
  ok = (env->request_id() == kReqId && proj == kProject) ? 1 : -1;
}

int main() {
  ipc::route{"st.agent.cmd", ipc::receiver}.clear();  // clean any stale shm
  ipc::route{"st.agent.state", ipc::receiver}.clear();
  std::atomic<int> appOk{0}, agentOk{0};
  std::thread a(appSide, std::ref(appOk));
  std::this_thread::sleep_for(std::chrono::milliseconds(50));  // let app connect first
  std::thread g(agentSide, std::ref(agentOk));
  a.join(); g.join();
  const bool pass = appOk == 1 && agentOk == 1;
  std::printf("%s\n", pass ? "CMD ROUND-TRIP PASS: FlatBuffers CommandEnvelope -> UiStateEnvelope over thoth-ipc shm"
                           : "FAIL");
  return pass ? 0 : 1;
}
