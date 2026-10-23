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
// `quit` is the window closing, or an agent asking the app to end (as closing its
// window would, so it goes through the same teardown as a person's quit).
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

	struct Data
	{
		std::vector<std::function<void()>> waiting;	// for `started`
		std::chrono::steady_clock::time_point createdAt = std::chrono::steady_clock::now();
		std::int64_t startupMs = -1;	// how long starting took, once known
	};

	static constexpr std::string_view name = "app";
	static constexpr State initial = State::Starting;
	static constexpr std::array states {
		named(State::Starting, "starting"), named(State::Running, "running"), named(State::Closing, "closing")};
	static constexpr std::array events {named(Event::Started, "started"), control(Event::Quit, "quit")};
	static constexpr auto transitions = std::to_array<StateMachine<State, Event>::Transition>({
		{State::Starting, Event::Started, State::Running},
		{State::Starting, Event::Quit, State::Closing},
		{State::Running, Event::Quit, State::Closing},
	});

	static std::string context(State state, const Data& data);
};

class AppStateMachine
{
public:
	using State = AppMachine::State;

	static std::shared_ptr<AppStateMachine> instance();

	// main has finished starting the app: runs what waited for it.
	void started();
	// The main window closed: the app is going away.
	void quitting();
	// Ends the app as closing its window would: what main installed does it.
	MachineFireResult quit(EventSource source);
	// How the app ends on `quit` (main's: leave the event loop).
	void onQuit(std::function<void()> quit);

	bool isStarting() const;
	bool isClosing() const;
	// Runs `work` once the app has started: now if it has, else on `started` (on
	// the thread that reports it, in the order asked).
	void whenStarted(std::function<void()> work);

	MachineSnapshot snapshot() const
	{
		return m_machine.snapshot();
	}
	MachineFireResult fire(const std::string& event, const std::string& args, EventSource source);

private:
	using Event = AppMachine::Event;

	ConcurrentStateMachine<AppMachine> m_machine;
	std::function<void()> m_quit;	// set once, before the app runs
};

#endif	  // APP_STATE_MACHINE_H
