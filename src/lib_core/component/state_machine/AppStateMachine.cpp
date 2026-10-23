#include "AppStateMachine.h"

#include <optional>

#include <glaze/glaze.hpp>

#include "MachineArgs.h"

// glaze reflects this for JSON; MSVC cannot reflect types in an anonymous namespace.
namespace app_machine_json
{
struct AppContext
{
	std::optional<std::int64_t> startup_ms;	// how long starting took
	std::optional<std::size_t> waiting;	// work queued for `started`
};
}	 // namespace app_machine_json

std::shared_ptr<AppStateMachine> AppStateMachine::instance()
{
	static const std::shared_ptr<AppStateMachine> machine = std::make_shared<AppStateMachine>();
	return machine;
}

void AppStateMachine::started()
{
	std::vector<std::function<void()>> waiting;
	{
		auto transaction = m_machine.transaction();
		AppMachine::Data& data = transaction.data();
		data.startupMs = std::chrono::duration_cast<std::chrono::milliseconds>(
							 std::chrono::steady_clock::now() - data.createdAt)
							 .count();
		waiting.swap(data.waiting);
		transaction.move(Event::Started);
	}
	for (const std::function<void()>& work: waiting)
	{
		work();
	}
}

void AppStateMachine::quitting()
{
	auto transaction = m_machine.transaction();
	transaction.data().waiting.clear();
	transaction.move(Event::Quit);
}

MachineFireResult AppStateMachine::quit(EventSource source)
{
	{
		auto transaction = m_machine.transaction();
		if (!transaction.accepts(Event::Quit))
		{
			return transaction.refuse(Event::Quit);
		}
		transaction.data().waiting.clear();
		transaction.move(Event::Quit, source);
	}
	if (!m_quit)
	{
		return {true, "closing, though nothing here ends the app"};
	}
	m_quit();
	return {true, "closing"};
}

void AppStateMachine::onQuit(std::function<void()> quit)
{
	m_quit = std::move(quit);
}

MachineFireResult AppStateMachine::fire(const std::string& event, const std::string& args, EventSource source)
{
	if (m_machine.controlNamed(event) != Event::Quit)
	{
		return m_machine.unknownEvent(event);
	}
	if (const auto parsed = parseArgs<machine_args::None>(event, args); !parsed)
	{
		return {false, parsed.error()};
	}
	return quit(source);
}

bool AppStateMachine::isStarting() const
{
	return m_machine.state() == State::Starting;
}

bool AppStateMachine::isClosing() const
{
	return m_machine.state() == State::Closing;
}

void AppStateMachine::whenStarted(std::function<void()> work)
{
	{
		auto transaction = m_machine.transaction();
		if (transaction.state() == State::Starting)
		{
			transaction.data().waiting.push_back(std::move(work));
			return;
		}
		if (transaction.state() == State::Closing)
		{
			return;
		}
	}
	work();
}

std::string AppMachine::context(State  /*state*/, const Data& data)
{
	app_machine_json::AppContext context;
	if (data.startupMs >= 0)
	{
		context.startup_ms = data.startupMs;
	}
	if (!data.waiting.empty())
	{
		context.waiting = data.waiting.size();
	}
	return glz::write_json(context).value_or("{}");
}
