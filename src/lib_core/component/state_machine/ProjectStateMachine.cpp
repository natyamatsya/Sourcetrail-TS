#include "ProjectStateMachine.h"

#include <optional>

#include "FilePath.h"
#include "IndexingStateMachine.h"
#include "MachineArgs.h"
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
}	 // namespace project_machine_json

std::shared_ptr<ProjectStateMachine> ProjectStateMachine::instance()
{
	static const std::shared_ptr<ProjectStateMachine> machine =
		std::make_shared<ProjectStateMachine>(AppStateMachine::instance());
	return machine;
}

ProjectStateMachine::ProjectStateMachine(std::shared_ptr<AppStateMachine> app): m_app(std::move(app)) {}

MachineFireResult ProjectStateMachine::load(const std::string& path, RefreshMode refresh, EventSource source)
{
	if (path.empty())
	{
		return {false, "load needs a project path"};
	}
	bool waiting = false;
	{
		Transaction transaction(*this);
		if (!transaction.accepts(Event::Load))
		{
			return transaction.refuse(Event::Load);
		}
		m_path = path;
		m_outcome.clear();
		waiting = m_app->isStarting();
		m_waitingForStartup = waiting;
		transaction.move(Event::Load, source);
	}

	// A load that ran while the app starts could find what it needs not there yet
	// (the language packages register late in startup): it waits for `started`.
	m_app->whenStarted([weak = weakSelf(), path, refresh]() {
		if (const auto self = weak.lock())
		{
			const auto lock = self->lock();
			self->m_waitingForStartup = false;
		}
		MessageLoadProject(FilePath(path), false, refresh).dispatch();
	});
	return {true, waiting ? "loading once the app has started" : "loading"};
}

MachineFireResult ProjectStateMachine::close(EventSource source)
{
	{
		Transaction transaction(*this);
		if (!transaction.accepts(Event::Close))
		{
			return transaction.refuse(Event::Close);
		}
		m_outcome.clear();
		transaction.move(Event::Close, source);
	}

	MessageCloseProject().dispatch();
	return {true, "closing"};
}

void ProjectStateMachine::requested(const std::string& path)
{
	Transaction transaction(*this);
	m_path = path;
	// Already loading when it was `load` that asked.
	if (transaction.state() != State::Loading)
	{
		m_outcome.clear();
		transaction.move(Event::Requested);
	}
}

void ProjectStateMachine::kept(const std::string& outcome)
{
	report(Event::Kept, &outcome);
}

void ProjectStateMachine::failed(const std::string& outcome)
{
	Transaction transaction(*this);
	m_outcome = outcome;
	if (transaction.accepts(Event::Failed))
	{
		m_path.clear();
		transaction.move(Event::Failed);
	}
}

void ProjectStateMachine::loaded()
{
	report(Event::Loaded);
}

void ProjectStateMachine::reloading()
{
	report(Event::Reloading);
}

void ProjectStateMachine::tabOpening()
{
	Transaction transaction(*this);
	if (!transaction.move(Event::TabOpening))
	{
		return;
	}
	// Moves on unless the tab showed (or anything else moved the machine) first.
	transaction.timeout(m_openingLimit, [this](Transaction& timedOut) {
		m_outcome = "the first tab did not show in time";
		timedOut.move(Event::TimedOut);
		timedOut.defer([]() {
			LOG_WARNING("project state machine: the tab a load opened was not activated in time; moving on to ready");
		});
	});
}

void ProjectStateMachine::shown()
{
	report(Event::Shown);
}

void ProjectStateMachine::closed()
{
	Transaction transaction(*this);
	if (transaction.accepts(Event::Closed))
	{
		m_path.clear();
		m_outcome = "closed";
		transaction.move(Event::Closed);
	}
}

void ProjectStateMachine::closeRefused(const std::string& outcome)
{
	report(Event::CloseRefused, &outcome);
}

void ProjectStateMachine::setOpeningLimit(std::chrono::milliseconds limit)
{
	const auto lock = this->lock();
	m_openingLimit = limit;
}

MachineFireResult ProjectStateMachine::fire(const std::string& event, const std::string& args, EventSource source)
{
	const std::optional<Event> control = controlNamed(event);
	if (control == Event::Load)
	{
		const auto parsed = parseArgs<project_machine_json::LoadArgs>(
			event, args, R"({"path": "...", "refresh": "updated" | "incomplete" | "all"})");
		if (!parsed)
		{
			return {false, parsed.error()};
		}
		RefreshMode refresh = RefreshMode::NONE;
		if (parsed->refresh)
		{
			const std::optional<RefreshMode> mode = IndexingStateMachine::modeFromName(*parsed->refresh);
			if (!mode)
			{
				return {false, std::format("unknown refresh mode '{}' (updated, incomplete, all)", *parsed->refresh)};
			}
			refresh = *mode;
		}
		return load(parsed->path, refresh, source);
	}
	if (control == Event::Close)
	{
		if (const auto parsed = parseArgs<machine_args::None>(event, args); !parsed)
		{
			return {false, parsed.error()};
		}
		return close(source);
	}
	return unknownEvent(event);
}

void ProjectStateMachine::report(Event event, const std::string* outcome)
{
	Transaction transaction(*this);
	if (!transaction.accepts(event))
	{
		return;
	}
	if (outcome != nullptr)
	{
		m_outcome = *outcome;
	}
	transaction.move(event);
}

std::string ProjectStateMachine::contextLocked() const
{
	project_machine_json::ProjectContext context;
	if (!m_path.empty())
	{
		context.path = m_path;
	}
	if (m_waitingForStartup)
	{
		context.waiting_for = "startup";
	}
	else if (stateLocked() == State::Opening)
	{
		context.waiting_for = "tab";
	}
	if (!m_outcome.empty())
	{
		context.outcome = m_outcome;
	}
	return glz::write_json(context).value_or("{}");
}
