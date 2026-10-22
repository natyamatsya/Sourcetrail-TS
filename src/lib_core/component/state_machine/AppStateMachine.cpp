#include "AppStateMachine.h"

#include <optional>

#include <glaze/glaze.hpp>

// glaze reflects this for JSON; MSVC cannot reflect types in an anonymous namespace.
namespace app_machine_json
{
struct AppContext
{
	std::optional<std::int64_t> startup_ms;	// how long starting took
	std::optional<std::size_t> waiting;	// work queued for `started`
};
}	 // namespace app_machine_json

AppStateMachine::AppStateMachine()
	: m_machine(
		  State::Starting,
		  {
			  {State::Starting, Event::Started, State::Running},
			  {State::Starting, Event::Quit, State::Closing},
			  {State::Running, Event::Quit, State::Closing},
		  })
{
}

std::shared_ptr<AppStateMachine> AppStateMachine::instance()
{
	static const std::shared_ptr<AppStateMachine> machine = std::make_shared<AppStateMachine>();
	return machine;
}

void AppStateMachine::started()
{
	std::vector<std::function<void()>> waiting;
	std::vector<MachineTransition> published;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_startupMs = std::chrono::duration_cast<std::chrono::milliseconds>(
						  std::chrono::steady_clock::now() - m_createdAt)
						  .count();
		waiting.swap(m_waiting);
		move(Event::Started, published);
	}
	for (const MachineTransition& transition: published)
	{
		StateMachineRegistry::instance().publish(transition);
	}
	for (const std::function<void()>& work: waiting)
	{
		work();
	}
}

void AppStateMachine::quitting()
{
	std::vector<MachineTransition> published;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_waiting.clear();
		move(Event::Quit, published);
	}
	for (const MachineTransition& transition: published)
	{
		StateMachineRegistry::instance().publish(transition);
	}
}

bool AppStateMachine::isStarting() const
{
	const std::lock_guard<std::mutex> lock(m_mutex);
	return m_machine.state() == State::Starting;
}

void AppStateMachine::whenStarted(std::function<void()> work)
{
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		if (m_machine.state() == State::Starting)
		{
			m_waiting.push_back(std::move(work));
			return;
		}
		if (m_machine.state() == State::Closing)
		{
			return;
		}
	}
	work();
}

std::string AppStateMachine::name() const
{
	return "app";
}

MachineSnapshot AppStateMachine::snapshot() const
{
	const std::lock_guard<std::mutex> lock(m_mutex);
	MachineSnapshot snapshot;
	snapshot.machine = name();
	snapshot.state = stateName(m_machine.state());
	snapshot.context = contextLocked();
	snapshot.revision = m_machine.revision();
	return snapshot;
}

MachineFireResult AppStateMachine::fire(const std::string& event, const std::string&  /*args*/, EventSource  /*source*/)
{
	return {false, "the app machine takes no events from outside ('" + event + "'): it follows the app starting and closing"};
}

const char* AppStateMachine::stateName(State state)
{
	switch (state)
	{
	case State::Starting:
		return "starting";
	case State::Running:
		return "running";
	case State::Closing:
		break;
	}
	return "closing";
}

const char* AppStateMachine::eventName(Event event)
{
	return event == Event::Started ? "started" : "quit";
}

void AppStateMachine::move(Event event, std::vector<MachineTransition>& published)
{
	const State from = m_machine.state();
	if (!m_machine.fire(event))
	{
		return;
	}
	published.push_back(
		{name(),
		 stateName(from),
		 stateName(m_machine.state()),
		 eventName(event),
		 EventSource::App,
		 contextLocked(),
		 m_machine.revision()});
}

std::string AppStateMachine::contextLocked() const
{
	app_machine_json::AppContext context;
	if (m_startupMs >= 0)
	{
		context.startup_ms = m_startupMs;
	}
	if (!m_waiting.empty())
	{
		context.waiting = m_waiting.size();
	}
	return glz::write_json(context).value_or("{}");
}
