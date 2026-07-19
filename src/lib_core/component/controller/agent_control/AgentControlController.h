#ifndef AGENT_CONTROL_CONTROLLER_H
#define AGENT_CONTROL_CONTROLLER_H

#include <memory>
#include <string>

// Drives Sourcetrail's UI from an AI agent over thoth-ipc shared-memory channels
// carrying FlatBuffers contracts (see context/DESIGN_AGENT_UI_CONTROL.md,
// schemas in ./schemas). Reads Command envelopes on st.agent.cmd and turns them
// into Message<T> dispatches; replies UiState snapshots on st.agent.state.
//
// Debug/flag-gated (SOURCETRAIL_AGENT_CONTROL): built and instantiated only in
// agent/CI builds, never in a shipped release input path.
//
// Threading: a dedicated reader jthread does only the blocking channel recv (a
// long-lived blocking loop must not occupy an ISchedulers::io() pool thread); all
// app interaction — Message dispatch and StorageAccess/StorageCache reads — is
// hopped onto ISchedulers::ui() (the Qt main thread), where the message bus and
// the non-thread-safe StorageCache expect to be touched. Cancellation is a
// stdexec::inplace_stop_source, mirroring TaskBuildIndex.
//
// Pimpl keeps thoth-ipc, FlatBuffers and stdexec headers out of this header.

class StorageAccess;

namespace execution
{
class ISchedulers;
}

class AgentControlController
{
public:
	// `instanceId` namespaces the thoth-ipc channels so multiple app instances can
	// run side by side (e.g. baseline vs candidate in a comparison test). Empty ->
	// the default `st.agent.*` names; otherwise `st.agent.<instanceId>.*`.
	AgentControlController(
		StorageAccess* storageAccess,
		execution::ISchedulers* schedulers,
		const std::string& instanceId = "");
	~AgentControlController();

	AgentControlController(const AgentControlController&) = delete;
	AgentControlController& operator=(const AgentControlController&) = delete;

	void startListening();
	void stopListening();
	bool isListening() const;

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

#endif	  // AGENT_CONTROL_CONTROLLER_H
