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

std::shared_ptr<AppStateMachine> AppStateMachine::instance()
{
	static const std::shared_ptr<AppStateMachine> machine = std::make_shared<AppStateMachine>();
	return machine;
}

void AppStateMachine::started()
{
	std::vector<std::function<void()>> waiting;
	{
		Transaction transaction(*this);
		m_startupMs = std::chrono::duration_cast<std::chrono::milliseconds>(
						  std::chrono::steady_clock::now() - m_createdAt)
						  .count();
		waiting.swap(m_waiting);
		transaction.move(Event::Started);
	}
	for (const std::function<void()>& work: waiting)
	{
		work();
	}
}

void AppStateMachine::quitting()
{
	Transaction transaction(*this);
	m_waiting.clear();
	transaction.move(Event::Quit);
}

bool AppStateMachine::isStarting() const
{
	return state() == State::Starting;
}

void AppStateMachine::whenStarted(std::function<void()> work)
{
	{
		Transaction transaction(*this);
		if (transaction.state() == State::Starting)
		{
			m_waiting.push_back(std::move(work));
			return;
		}
		if (transaction.state() == State::Closing)
		{
			return;
		}
	}
	work();
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
