// Cross-process variant: app and agent are SEPARATE processes (fork), talking only
// through thoth-ipc named shared memory + FlatBuffers. This is the real deployment.
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <string>
#include <vector>

#include <libipc/ipc.h>
#include <flatbuffers/flatbuffers.h>
#include "agent_command_generated.h"
#include "agent_state_generated.h"

using namespace Sourcetrail::Agent;
static constexpr uint64_t kReqId = 7;
static const char* kProject = "/tmp/demo.srctrl.toml";

static int appProc() {  // parent
  ipc::route cmd{"st.agent.cmd", ipc::receiver};
  ipc::route state{"st.agent.state", ipc::sender};
  ipc::buff_t in;
  for (int i = 0; i < 100 && in.empty(); ++i) in = cmd.recv(100);
  if (in.empty()) { std::printf("app: timeout\n"); return 1; }
  flatbuffers::Verifier v(static_cast<const uint8_t*>(in.data()), in.size());
  if (!VerifyCommandEnvelopeBuffer(v)) { std::printf("app: bad buffer\n"); return 1; }
  const auto* env = GetCommandEnvelope(in.data());
  std::printf("app(pid %d): recv Command=%s id=%llu\n", getpid(),
              EnumNameCommand(env->command_type()), (unsigned long long)env->request_id());
  flatbuffers::FlatBufferBuilder b;
  auto name = b.CreateString("app::Foo");
  std::vector<flatbuffers::Offset<NodeRef>> nodes{ CreateNodeRef(b, 1, name, 8, 0) };
  auto ui = CreateUiState(b, b.CreateString(kProject), b.CreateVector(nodes));
  b.Finish(CreateUiStateEnvelope(b, env->request_id(), ui));
  state.wait_for_recv(1, 2000);
  state.send(b.GetBufferPointer(), b.GetSize());
  return env->command_type() == Command_GetUiState ? 0 : 1;
}

static int agentProc() {  // child
  ipc::route cmd{"st.agent.cmd", ipc::sender};
  ipc::route state{"st.agent.state", ipc::receiver};
  flatbuffers::FlatBufferBuilder b;
  auto gus = CreateGetUiState(b);
  b.Finish(CreateCommandEnvelope(b, kReqId, Command_GetUiState, gus.Union()));
  if (!cmd.wait_for_recv(1, 2000)) { std::printf("agent: no app receiver\n"); return 1; }
  cmd.send(b.GetBufferPointer(), b.GetSize());
  ipc::buff_t in;
  for (int i = 0; i < 100 && in.empty(); ++i) in = state.recv(100);
  if (in.empty()) { std::printf("agent: timeout\n"); return 1; }
  flatbuffers::Verifier v(static_cast<const uint8_t*>(in.data()), in.size());
  if (!VerifyUiStateEnvelopeBuffer(v)) { std::printf("agent: bad buffer\n"); return 1; }
  const auto* env = GetUiStateEnvelope(in.data());
  const std::string proj = env->state()->project_file_path()->str();
  std::printf("agent(pid %d): recv UiState id=%llu project='%s'\n", getpid(),
              (unsigned long long)env->request_id(), proj.c_str());
  return (env->request_id() == kReqId && proj == kProject) ? 0 : 1;
}

int main() {
  ipc::route{"st.agent.cmd", ipc::receiver}.clear();
  ipc::route{"st.agent.state", ipc::receiver}.clear();
  pid_t pid = fork();
  if (pid == 0) { _exit(agentProc()); }
  int appRc = appProc();
  int st = 0; waitpid(pid, &st, 0);
  int agentRc = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
  bool pass = appRc == 0 && agentRc == 0;
  std::printf("%s\n", pass ? "CROSS-PROCESS CMD ROUND-TRIP PASS (two processes, shm only)" : "FAIL");
  return pass ? 0 : 1;
}
