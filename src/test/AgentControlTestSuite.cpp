#include "Catch2.hpp"

#if defined(SOURCETRAIL_AGENT_CONTROL)

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <flatbuffers/flatbuffers.h>
#include <thoth-ipc/ipc.h>

#include "agent_command_generated.h"
#include "agent_event_generated.h"
#include "agent_state_generated.h"

#include "AgentControlController.h"
#include "AppStateMachine.h"
#include "ActivationStateMachine.h"
#include "ISchedulers.h"
#include "IndexingStateMachine.h"
#include "MessageQueue.h"
#include "ProjectStateMachine.h"
#include "QuestionStateMachine.h"
#include "RemoteStateMachine.h"
#include "StdexecPrelude.h"
#include "StorageAccessProxy.h"

namespace fb = Sourcetrail::Agent;

namespace
{
using namespace std::chrono_literals;

// Every context on one pool: the tests have no Qt GUI thread.
class PoolSchedulers: public execution::ISchedulers
{
public:
	execution::AnyScheduler io() const override
	{
		return m_pool.get_scheduler();
	}
	execution::AnyScheduler compute() const override
	{
		return m_pool.get_scheduler();
	}
	execution::AnyScheduler ui() const override
	{
		return m_pool.get_scheduler();
	}

private:
	mutable exec::static_thread_pool m_pool {2};
};

// The bridge's end of a controller's channels.
class Agent
{
public:
	explicit Agent(const std::string& instanceId)
		: m_cmd(("st.agent." + instanceId + ".cmd").c_str(), thoth::sender)
		, m_events(("st.agent." + instanceId + ".events").c_str(), thoth::receiver)
		, m_state(("st.agent." + instanceId + ".state").c_str(), thoth::receiver)
	{
	}

	// Sends the command `make` builds; returns its request id.
	template <typename Make>
	std::uint64_t send(Make make)
	{
		flatbuffers::FlatBufferBuilder builder;
		const std::uint64_t requestId = ++m_lastRequestId;
		const auto [type, command] = make(builder);
		builder.Finish(fb::CreateCommandEnvelope(builder, requestId, type, command));
		REQUIRE(m_cmd.send(builder.GetBufferPointer(), builder.GetSize(), 2000));
		return requestId;
	}

	void sendBytes(const std::vector<std::uint8_t>& bytes)
	{
		REQUIRE(m_cmd.send(bytes.data(), bytes.size(), 2000));
	}

	// The events read until one `until` accepts (that one last), or all read within
	// `timeout` when none does.
	std::vector<std::vector<std::uint8_t>> eventsUntil(
		const std::function<bool(const fb::EventEnvelope&)>& until,
		std::chrono::milliseconds timeout = 3s)
	{
		std::vector<std::vector<std::uint8_t>> events;
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (std::chrono::steady_clock::now() < deadline)
		{
			const thoth::buff_t buffer = m_events.recv(50);
			if (buffer.empty())
			{
				continue;
			}
			const auto* bytes = static_cast<const std::uint8_t*>(buffer.data());
			events.emplace_back(bytes, bytes + buffer.size());
			if (until(*fb::GetEventEnvelope(events.back().data())))
			{
				break;
			}
		}
		return events;
	}

	// The ack of `requestId`: whether it was accepted, and why not.
	std::optional<std::pair<bool, std::string>> ack(std::uint64_t requestId, std::chrono::milliseconds timeout = 3s)
	{
		const auto events = eventsUntil(
			[requestId](const fb::EventEnvelope& envelope) {
				const auto* result = envelope.event_as_CommandResult();
				return result != nullptr && result->request_id() == requestId;
			},
			timeout);
		if (events.empty())
		{
			return std::nullopt;
		}
		const auto* result = fb::GetEventEnvelope(events.back().data())->event_as_CommandResult();
		if (result == nullptr || result->request_id() != requestId)
		{
			return std::nullopt;
		}
		return std::pair {result->ok(), result->message() != nullptr ? result->message()->str() : ""};
	}

	// The UiState of `requestId`, as its app state, if it came within `timeout`.
	std::optional<fb::AppState> uiState(std::uint64_t requestId, std::chrono::milliseconds timeout = 3s)
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (std::chrono::steady_clock::now() < deadline)
		{
			const thoth::buff_t buffer = m_state.recv(50);
			if (buffer.empty())
			{
				continue;
			}
			const auto* envelope = fb::GetUiStateEnvelope(buffer.data());
			if (envelope->request_id() == requestId && envelope->state() != nullptr)
			{
				return envelope->state()->app_state();
			}
		}
		return std::nullopt;
	}

private:
	thoth::route m_cmd;
	thoth::route m_events;
	thoth::route m_state;
	std::uint64_t m_lastRequestId = 0;
};

// A listening controller under its own channel names, and the agent's end of them.
// The message queue does not run unless a test starts it: commands that wait in it
// stay unanswered, as they do while the app's queue is busy.
struct ControlledApp
{
	ControlledApp()
		: instanceId("test" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))
		, controller(&storage, &schedulers, instanceId)
	{
		// What Application registers at startup.
		StateMachineRegistry::instance().add(AppStateMachine::instance());
		StateMachineRegistry::instance().add(ProjectStateMachine::instance());
		StateMachineRegistry::instance().add(IndexingStateMachine::instance());
		StateMachineRegistry::instance().add(QuestionStateMachine::instance());
		StateMachineRegistry::instance().add(ActivationStateMachine::instance());
		// Started, as main reports it: until then the app is Loading.
		if (AppStateMachine::instance()->isStarting())
		{
			AppStateMachine::instance()->started();
		}
		controller.startListening();
		agent.emplace(instanceId);	// the controller cleared its channels: connect after it
	}

	~ControlledApp()
	{
		controller.stopListening();
	}

	PoolSchedulers schedulers;
	StorageAccessProxy storage;
	std::string instanceId;
	AgentControlController controller;
	std::optional<Agent> agent;
};

struct MessageLoop
{
	MessageLoop()
	{
		MessageQueue::getInstance()->startMessageLoopThreaded();
	}
	~MessageLoop()
	{
		MessageQueue::getInstance()->stopMessageLoop();
	}
};

std::pair<fb::Command, flatbuffers::Offset<void>> getInfo(flatbuffers::FlatBufferBuilder& builder)
{
	return {fb::Command_GetInfo, fb::CreateGetInfo(builder).Union()};
}

std::pair<fb::Command, flatbuffers::Offset<void>> getUiState(flatbuffers::FlatBufferBuilder& builder)
{
	return {fb::Command_GetUiState, fb::CreateGetUiState(builder).Union()};
}

auto getMachines(const char* machine)
{
	return [machine](flatbuffers::FlatBufferBuilder& builder) {
		return std::pair {fb::Command_GetMachines, fb::CreateGetMachinesDirect(builder, machine).Union()};
	};
}

auto loadProject(const char* path)
{
	return [path](flatbuffers::FlatBufferBuilder& builder) {
		return std::pair {fb::Command_LoadProject, fb::CreateLoadProjectDirect(builder, path).Union()};
	};
}

// The machine names a MachineStates reply of `requestId` among `events` lists.
std::vector<std::string> machinesIn(const std::vector<std::vector<std::uint8_t>>& events, std::uint64_t requestId)
{
	for (const auto& bytes: events)
	{
		const auto* states = fb::GetEventEnvelope(bytes.data())->event_as_MachineStates();
		if (states != nullptr && states->request_id() == requestId)
		{
			std::vector<std::string> names;
			if (states->machines() != nullptr)
			{
				for (const auto* machine: *states->machines())
				{
					names.push_back(machine->machine()->str());
				}
			}
			return names;
		}
	}
	return {"(no MachineStates)"};
}

bool isMachineStatesOf(const fb::EventEnvelope& envelope, std::uint64_t requestId)
{
	const auto* states = envelope.event_as_MachineStates();
	return states != nullptr && states->request_id() == requestId;
}
}	 // namespace

TEST_CASE("agent control says which app it is")
{
	ControlledApp app;

	const std::uint64_t request = app.agent->send(getInfo);
	const auto events = app.agent->eventsUntil([request](const fb::EventEnvelope& envelope) {
		const auto* info = envelope.event_as_AppInfo();
		return info != nullptr && info->request_id() == request;
	});
	REQUIRE_FALSE(events.empty());
	const auto* info = fb::GetEventEnvelope(events.back().data())->event_as_AppInfo();
	REQUIRE(info != nullptr);
	REQUIRE(info->protocol_version() == static_cast<std::uint32_t>(fb::ProtocolVersion_Current));
	REQUIRE(info->instance_id()->str() == app.instanceId);
}

TEST_CASE("agent control state machines answer while the queue is busy")
{
	ControlledApp app;

	const std::uint64_t all = app.agent->send(getMachines(""));
	const auto allEvents =
		app.agent->eventsUntil([all](const fb::EventEnvelope& envelope) { return isMachineStatesOf(envelope, all); });
	const std::vector<std::string> names = machinesIn(allEvents, all);
	for (const char* machine: {"app", "project", "indexing", "question", "activation"})
	{
		REQUIRE(std::ranges::contains(names, machine));
	}

	const std::uint64_t unknown = app.agent->send(getMachines("nope"));
	const auto unknownEvents = app.agent->eventsUntil(
		[unknown](const fb::EventEnvelope& envelope) { return isMachineStatesOf(envelope, unknown); });
	REQUIRE(machinesIn(unknownEvents, unknown).empty());	// a refusal still has its reply
	const auto refusal = std::ranges::find_if(unknownEvents, [unknown](const std::vector<std::uint8_t>& bytes) {
		const auto* result = fb::GetEventEnvelope(bytes.data())->event_as_CommandResult();
		return result != nullptr && result->request_id() == unknown;
	});
	REQUIRE(refusal != unknownEvents.end());
	const auto* refused = fb::GetEventEnvelope(refusal->data())->event_as_CommandResult();
	REQUIRE_FALSE(refused->ok());
	REQUIRE(refused->message()->str().starts_with("no state machine 'nope' ("));

	const std::uint64_t answer = app.agent->send([](flatbuffers::FlatBufferBuilder& builder) {
		return std::pair {
			fb::Command_SendMachineEvent,
			fb::CreateSendMachineEventDirect(builder, "question", "answer", R"({"label":"Yes"})").Union()};
	});
	const auto answered = app.agent->ack(answer);
	REQUIRE(answered);
	REQUIRE_FALSE(answered->first);	// nothing was asked
}

TEST_CASE("agent control acknowledges a load while the queue is busy")
{
	ControlledApp app;

	const std::uint64_t pathless = app.agent->send(loadProject(nullptr));
	REQUIRE(app.agent->ack(pathless) == std::pair {false, std::string("rejected: load_project: missing project path")});

	// The project machine is loading on behalf of the request before the ack; the
	// load it dispatched waits in the queue, which nothing here runs.
	const std::uint64_t load = app.agent->send(loadProject("a.srctrl.toml"));
	const auto events = app.agent->eventsUntil([load](const fb::EventEnvelope& envelope) {
		const auto* result = envelope.event_as_CommandResult();
		return result != nullptr && result->request_id() == load;
	});
	REQUIRE_FALSE(events.empty());
	const auto* acked = fb::GetEventEnvelope(events.back().data())->event_as_CommandResult();
	REQUIRE(acked != nullptr);
	REQUIRE(acked->ok());
	const auto loading = std::ranges::find_if(events, [](const std::vector<std::uint8_t>& bytes) {
		const auto* transition = fb::GetEventEnvelope(bytes.data())->event_as_MachineTransition();
		return transition != nullptr && transition->machine()->str() == "project" && transition->to()->str() == "loading";
	});
	REQUIRE(loading != events.end());
	REQUIRE(fb::GetEventEnvelope(loading->data())->caused_by() == load);
	REQUIRE(fb::GetEventEnvelope(loading->data())->event_as_MachineTransition()->source()->str() == "agent");

	const std::uint64_t again = app.agent->send(loadProject("b.srctrl.toml"));
	const auto refused = app.agent->ack(again);
	REQUIRE(refused);
	REQUIRE_FALSE(refused->first);
	REQUIRE(refused->second.starts_with("rejected: "));

	ProjectStateMachine::instance()->failed("the test is over");	// back to no_project
}

TEST_CASE("agent control answers a queued command once the queue gets to it")
{
	ControlledApp app;

	const std::uint64_t request = app.agent->send(getUiState);
	REQUIRE_FALSE(app.agent->uiState(request, 300ms));

	const MessageLoop loop;
	REQUIRE(app.agent->uiState(request) == fb::AppState_NoProject);
}

TEST_CASE("agent control refuses what needs a project")
{
	ControlledApp app;
	const MessageLoop loop;

	const std::uint64_t request = app.agent->send([](flatbuffers::FlatBufferBuilder& builder) {
		return std::pair {fb::Command_ActivateNode, fb::CreateActivateNode(builder, 0, 42).Union()};
	});
	REQUIRE(app.agent->ack(request) == std::pair {false, std::string("rejected: no project loaded")});
}

TEST_CASE("agent control skips a malformed command")
{
	ControlledApp app;
	const MessageLoop loop;

	app.agent->sendBytes({0x01, 0x02, 0x03});
	const std::uint64_t request = app.agent->send(getUiState);
	REQUIRE(app.agent->uiState(request) == fb::AppState_NoProject);
}

#endif	  // SOURCETRAIL_AGENT_CONTROL
