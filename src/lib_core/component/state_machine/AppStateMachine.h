#ifndef APP_STATE_MACHINE_H
#define APP_STATE_MACHINE_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ConcurrentStateMachine.h"

// The application's own lifecycle:
//
//   starting --started--> running --quit--> closing
//
// "starting" lasts from Application::createInstance() until main has finished
// starting the app: the language packages registered, the settings prefilled, the
// startup load queued. Work that needs a started app (a project load an agent asks
// for while it starts) waits for `started` instead of racing it (whenStarted).
struct AppMachine
{
	enum class State
	{
		Starting,
		Running,
		Closing
	};
	enum class Event
	{
		Started,
		Quit
	};

	static constexpr std::string_view name = "app";
	static constexpr State initial = State::Starting;
	static constexpr std::array states {
		named(State::Starting, "starting"), named(State::Running, "running"), named(State::Closing, "closing")};
	static constexpr std::array events {named(Event::Started, "started"), named(Event::Quit, "quit")};
	static constexpr auto transitions = std::to_array<StateMachine<State, Event>::Transition>({
		{State::Starting, Event::Started, State::Running},
		{State::Starting, Event::Quit, State::Closing},
		{State::Running, Event::Quit, State::Closing},
	});
};

class AppStateMachine: public ConcurrentStateMachine<AppMachine>
{
public:
	static std::shared_ptr<AppStateMachine> instance();

	// main has finished starting the app: runs what waited for it.
	void started();
	// The main window closed: the app is going away.
	void quitting();

	bool isStarting() const;
	// Runs `work` once the app has started: now if it has, else on `started` (on
	// the thread that reports it, in the order asked).
	void whenStarted(std::function<void()> work);

private:
	std::string contextLocked() const override;

	std::vector<std::function<void()>> m_waiting;	// for `started`
	const std::chrono::steady_clock::time_point m_createdAt = std::chrono::steady_clock::now();
	std::int64_t m_startupMs = -1;	// how long starting took, once known
};

#endif	  // APP_STATE_MACHINE_H
