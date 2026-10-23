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
		auto transaction = m_machine.transaction();
		if (!transaction.accepts(Event::Load))
		{
			return transaction.refuse(Event::Load);
		}
		ProjectMachine::Data& data = transaction.data();
		data.path = path;
		data.outcome.clear();
		waiting = m_app->isStarting();
		data.waitingForStartup = waiting;
		transaction.move(Event::Load, source);
	}

	// A load that ran while the app starts could find what it needs not there yet
	// (the language packages register late in startup): it waits for `started`, and
	// is still on behalf of what asked for it.
	m_app->whenStarted(withCurrentCause([weak = m_machine.weak(), path, refresh]() {
		if (const auto machine = weak.lock())
		{
			machine->transaction().data().waitingForStartup = false;
		}
		MessageLoadProject(FilePath(path), false, refresh).dispatch();
	}));
	return {true, waiting ? "loading once the app has started" : "loading"};
}

MachineFireResult ProjectStateMachine::close(EventSource source)
{
	{
		auto transaction = m_machine.transaction();
		if (!transaction.accepts(Event::Close))
		{
			return transaction.refuse(Event::Close);
		}
		transaction.data().outcome.clear();
		transaction.move(Event::Close, source);
	}

	MessageCloseProject().dispatch();
	return {true, "closing"};
}

void ProjectStateMachine::requested(const std::string& path)
{
	auto transaction = m_machine.transaction();
	transaction.data().path = path;
	// Already loading when it was `load` that asked.
	if (transaction.state() != State::Loading)
	{
		transaction.data().outcome.clear();
		transaction.move(Event::Requested);
	}
}

void ProjectStateMachine::kept(const std::string& outcome)
{
	report(Event::Kept, &outcome);
}

void ProjectStateMachine::failed(const std::string& outcome)
{
	auto transaction = m_machine.transaction();
	transaction.data().outcome = outcome;
	if (transaction.accepts(Event::Failed))
	{
		transaction.data().path.clear();
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
	auto transaction = m_machine.transaction();
	if (!transaction.move(Event::TabOpening))
	{
		return;
	}
	// Moves on unless the tab showed (or anything else moved the machine) first.
	transaction.timeout(transaction.data().openingLimit, [](auto& timedOut) {
		timedOut.data().outcome = "the first tab did not show in time";
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
	auto transaction = m_machine.transaction();
	if (transaction.accepts(Event::Closed))
	{
		transaction.data().path.clear();
		transaction.data().outcome = "closed";
		transaction.move(Event::Closed);
	}
}

void ProjectStateMachine::closeRefused(const std::string& outcome)
{
	report(Event::CloseRefused, &outcome);
}

void ProjectStateMachine::setOpeningLimit(std::chrono::milliseconds limit)
{
	m_machine.transaction().data().openingLimit = limit;
}

MachineFireResult ProjectStateMachine::fire(const std::string& event, const std::string& args, EventSource source)
{
	const std::optional<Event> control = m_machine.controlNamed(event);
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
	return m_machine.unknownEvent(event);
}

void ProjectStateMachine::report(Event event, const std::string* outcome)
{
	auto transaction = m_machine.transaction();
	if (!transaction.accepts(event))
	{
		return;
	}
	if (outcome != nullptr)
	{
		transaction.data().outcome = *outcome;
	}
	transaction.move(event);
}

std::string ProjectMachine::context(State state, const Data& data)
{
	project_machine_json::ProjectContext context;
	if (!data.path.empty())
	{
		context.path = data.path;
	}
	if (data.waitingForStartup)
	{
		context.waiting_for = "startup";
	}
	else if (state == State::Opening)
	{
		context.waiting_for = "tab";
	}
	if (!data.outcome.empty())
	{
		context.outcome = data.outcome;
	}
	return glz::write_json(context).value_or("{}");
}
