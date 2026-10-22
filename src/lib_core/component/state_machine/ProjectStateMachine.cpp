#include "ProjectStateMachine.h"

#include <optional>
#include <thread>

#include <glaze/glaze.hpp>

#include "FilePath.h"
#include "IndexingStateMachine.h"
#include "MessageCloseProject.h"
#include "MessageLoadProject.h"
#include "logging.h"

// glaze reflects these for JSON; MSVC cannot reflect types in an anonymous namespace.
namespace project_machine_json
{
struct ProjectContext
{
	std::optional<std::string> path;
	std::optional<std::string> waiting_for;	// "startup" (the app is starting), "tab" (its first activation)
	std::optional<std::string> outcome;	// how the last load or close ended
};

struct LoadArgs
{
	std::string path;
	std::optional<std::string> refresh;	// "updated" | "incomplete" | "all" once open
};

struct NoArgs
{
};
}	 // namespace project_machine_json

namespace
{
using namespace project_machine_json;
using State = ProjectStateMachine::State;
using Event = ProjectStateMachine::Event;

bool isControl(Event event)
{
	return event == Event::Load || event == Event::Close;
}

MachineFireResult notAccepted(Event event, State state)
{
	return {
		false,
		std::string("'") + ProjectStateMachine::eventName(event) + "' is not accepted in state '" +
			ProjectStateMachine::stateName(state) + "'"};
}

void publishAll(const std::vector<MachineTransition>& published)
{
	for (const MachineTransition& transition: published)
	{
		StateMachineRegistry::instance().publish(transition);
	}
}
}	 // namespace

std::shared_ptr<ProjectStateMachine> ProjectStateMachine::instance()
{
	static const std::shared_ptr<ProjectStateMachine> machine =
		std::make_shared<ProjectStateMachine>(AppStateMachine::instance());
	return machine;
}

ProjectStateMachine::ProjectStateMachine(std::shared_ptr<AppStateMachine> app)
	: m_app(std::move(app))
	, m_machine(
		  State::NoProject,
		  {
			  {State::NoProject, Event::Load, State::Loading},
			  {State::NoProject, Event::Requested, State::Loading},

			  {State::Loading, Event::Kept, State::Ready},
			  {State::Loading, Event::Failed, State::NoProject},
			  {State::Loading, Event::Loaded, State::Ready},
			  {State::Loading, Event::TabOpening, State::Opening},

			  {State::Opening, Event::Shown, State::Ready},
			  {State::Opening, Event::TimedOut, State::Ready},
			  {State::Opening, Event::Requested, State::Loading},

			  {State::Ready, Event::Load, State::Loading},
			  {State::Ready, Event::Requested, State::Loading},
			  {State::Ready, Event::TabOpening, State::Opening},
			  {State::Ready, Event::Reloading, State::Loading},
			  {State::Ready, Event::Close, State::Unloading},
			  {State::Ready, Event::Closed, State::NoProject},

			  {State::Unloading, Event::Closed, State::NoProject},
			  {State::Unloading, Event::CloseRefused, State::Ready},
		  })
{
}

MachineFireResult ProjectStateMachine::load(const std::string& path, RefreshMode refresh, EventSource source)
{
	if (path.empty())
	{
		return {false, "load needs a project path"};
	}
	std::vector<MachineTransition> published;
	bool waiting = false;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_machine.accepts(Event::Load))
		{
			return notAccepted(Event::Load, m_machine.state());
		}
		m_path = path;
		m_outcome.clear();
		waiting = m_app->isStarting();
		m_waitingForStartup = waiting;
		move(Event::Load, source, published);
	}
	publishAll(published);

	// A load that ran while the app starts could find what it needs not there yet
	// (the language packages register late in startup): it waits for `started`.
	m_app->whenStarted([weak = weak_from_this(), path, refresh]() {
		if (const auto self = weak.lock())
		{
			const std::lock_guard<std::mutex> lock(self->m_mutex);
			self->m_waitingForStartup = false;
		}
		MessageLoadProject(FilePath(path), false, refresh).dispatch();
	});
	return {true, waiting ? "loading once the app has started" : "loading"};
}

MachineFireResult ProjectStateMachine::close(EventSource source)
{
	std::vector<MachineTransition> published;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_machine.accepts(Event::Close))
		{
			return notAccepted(Event::Close, m_machine.state());
		}
		m_outcome.clear();
		move(Event::Close, source, published);
	}
	publishAll(published);

	MessageCloseProject().dispatch();
	return {true, "closing"};
}

void ProjectStateMachine::requested(const std::string& path)
{
	std::vector<MachineTransition> published;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_path = path;
		// Already loading when it was `load` that asked.
		if (m_machine.state() != State::Loading)
		{
			m_outcome.clear();
			move(Event::Requested, EventSource::App, published);
		}
	}
	publishAll(published);
}

void ProjectStateMachine::kept(const std::string& outcome)
{
	moveAndPublish(Event::Kept, &outcome);
}

void ProjectStateMachine::failed(const std::string& outcome)
{
	std::vector<MachineTransition> published;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_outcome = outcome;
		if (m_machine.accepts(Event::Failed))
		{
			m_path.clear();
			move(Event::Failed, EventSource::App, published);
		}
	}
	publishAll(published);
}

void ProjectStateMachine::loaded()
{
	moveAndPublish(Event::Loaded);
}

void ProjectStateMachine::reloading()
{
	moveAndPublish(Event::Reloading);
}

void ProjectStateMachine::tabOpening()
{
	std::uint64_t revision = 0;
	std::chrono::milliseconds limit {0};
	{
		std::vector<MachineTransition> published;
		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			if (!move(Event::TabOpening, EventSource::App, published))
			{
				return;
			}
			revision = m_machine.revision();
			limit = m_openingLimit;
		}
		publishAll(published);
	}
	// The timer event: moves on unless the tab showed (or anything else moved the
	// machine) in the meantime.
	std::thread([weak = weak_from_this(), revision, limit]() {
		std::this_thread::sleep_for(limit);
		if (const auto self = weak.lock())
		{
			self->openingTimedOut(revision);
		}
	}).detach();
}

void ProjectStateMachine::shown()
{
	moveAndPublish(Event::Shown);
}

void ProjectStateMachine::closed()
{
	std::vector<MachineTransition> published;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		if (m_machine.accepts(Event::Closed))
		{
			m_path.clear();
			m_outcome = "closed";
			move(Event::Closed, EventSource::App, published);
		}
	}
	publishAll(published);
}

void ProjectStateMachine::closeRefused(const std::string& outcome)
{
	moveAndPublish(Event::CloseRefused, &outcome);
}

void ProjectStateMachine::setOpeningLimit(std::chrono::milliseconds limit)
{
	const std::lock_guard<std::mutex> lock(m_mutex);
	m_openingLimit = limit;
}

std::string ProjectStateMachine::name() const
{
	return "project";
}

MachineSnapshot ProjectStateMachine::snapshot() const
{
	const std::lock_guard<std::mutex> lock(m_mutex);
	MachineSnapshot snapshot;
	snapshot.machine = name();
	snapshot.state = stateName(m_machine.state());
	for (const Event event: m_machine.acceptedEvents())
	{
		if (isControl(event))
		{
			snapshot.events.push_back(eventName(event));
		}
	}
	snapshot.context = contextLocked();
	snapshot.revision = m_machine.revision();
	return snapshot;
}

MachineFireResult ProjectStateMachine::fire(const std::string& event, const std::string& args, EventSource source)
{
	const std::string text = args.empty() ? std::string("{}") : args;
	if (event == "load")
	{
		LoadArgs parsed;
		if (const auto error = glz::read_json(parsed, text))
		{
			return {false, "load: " + glz::format_error(error, text) + R"( (expected {"path": "...", "refresh": "updated" | "incomplete" | "all"}))"};
		}
		RefreshMode refresh = RefreshMode::NONE;
		if (parsed.refresh)
		{
			const std::optional<RefreshMode> mode = IndexingStateMachine::modeFromName(*parsed.refresh);
			if (!mode)
			{
				return {false, "unknown refresh mode '" + *parsed.refresh + "' (updated, incomplete, all)"};
			}
			refresh = *mode;
		}
		return load(parsed.path, refresh, source);
	}
	if (event == "close")
	{
		NoArgs parsed;
		if (const auto error = glz::read_json(parsed, text))
		{
			return {false, "close takes no arguments: " + glz::format_error(error, text)};
		}
		return close(source);
	}
	return {false, "unknown event '" + event + "' (load, close; the others come from the application)"};
}

const char* ProjectStateMachine::stateName(State state)
{
	switch (state)
	{
	case State::NoProject:
		return "no_project";
	case State::Loading:
		return "loading";
	case State::Opening:
		return "opening";
	case State::Ready:
		return "ready";
	case State::Unloading:
		return "unloading";
	}
	return "?";
}

const char* ProjectStateMachine::eventName(Event event)
{
	switch (event)
	{
	case Event::Load:
		return "load";
	case Event::Close:
		return "close";
	case Event::Requested:
		return "requested";
	case Event::Kept:
		return "kept";
	case Event::Failed:
		return "failed";
	case Event::Loaded:
		return "loaded";
	case Event::TabOpening:
		return "tab_opening";
	case Event::Reloading:
		return "reloading";
	case Event::Shown:
		return "shown";
	case Event::TimedOut:
		return "timed_out";
	case Event::Closed:
		return "closed";
	case Event::CloseRefused:
		return "close_refused";
	}
	return "?";
}

bool ProjectStateMachine::move(Event event, EventSource source, std::vector<MachineTransition>& published)
{
	const State from = m_machine.state();
	if (!m_machine.fire(event))
	{
		return false;
	}
	published.push_back(
		{name(),
		 stateName(from),
		 stateName(m_machine.state()),
		 eventName(event),
		 source,
		 contextLocked(),
		 m_machine.revision()});
	return true;
}

void ProjectStateMachine::moveAndPublish(Event event, const std::string* outcome)
{
	std::vector<MachineTransition> published;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_machine.accepts(event))
		{
			return;
		}
		if (outcome != nullptr)
		{
			m_outcome = *outcome;
		}
		move(event, EventSource::App, published);
	}
	publishAll(published);
}

void ProjectStateMachine::openingTimedOut(std::uint64_t revision)
{
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		if (m_machine.state() != State::Opening || m_machine.revision() != revision)
		{
			return;
		}
	}
	LOG_WARNING("project state machine: the tab a load opened was not activated in time; moving on to ready");
	const std::string outcome = "the first tab did not show in time";
	moveAndPublish(Event::TimedOut, &outcome);
}

std::string ProjectStateMachine::contextLocked() const
{
	ProjectContext context;
	if (!m_path.empty())
	{
		context.path = m_path;
	}
	if (m_waitingForStartup)
	{
		context.waiting_for = "startup";
	}
	else if (m_machine.state() == State::Opening)
	{
		context.waiting_for = "tab";
	}
	if (!m_outcome.empty())
	{
		context.outcome = m_outcome;
	}
	return glz::write_json(context).value_or("{}");
}
