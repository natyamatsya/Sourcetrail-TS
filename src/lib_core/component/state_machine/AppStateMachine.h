#ifndef APP_STATE_MACHINE_H
#define APP_STATE_MACHINE_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "RemoteStateMachine.h"
#include "StateMachine.h"

// The application's own lifecycle:
//
//   starting --started--> running --quit--> closing
//
// "starting" lasts from Application::createInstance() until main has finished
// starting the app: the language packages registered, the settings prefilled, the
// startup load queued. Work that needs a started app (a project load an agent asks
// for while it starts) waits for `started` instead of racing it (whenStarted).
class AppStateMachine: public RemoteStateMachine
{
public:
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

	static std::shared_ptr<AppStateMachine> instance();

	AppStateMachine();

	// main has finished starting the app: runs what waited for it.
	void started();
	// The main window closed: the app is going away.
	void quitting();

	bool isStarting() const;
	// Runs `work` once the app has started: now if it has, else on `started` (on
	// the thread that reports it, in the order asked).
	void whenStarted(std::function<void()> work);

	std::string name() const override;
	MachineSnapshot snapshot() const override;
	MachineFireResult fire(const std::string& event, const std::string& args, EventSource source) override;

	static const char* stateName(State state);
	static const char* eventName(Event event);

private:
	void move(Event event, std::vector<MachineTransition>& published);
	std::string contextLocked() const;

	mutable std::mutex m_mutex;
	StateMachine<State, Event> m_machine;
	std::vector<std::function<void()>> m_waiting;	// for `started`
	const std::chrono::steady_clock::time_point m_createdAt = std::chrono::steady_clock::now();
	std::int64_t m_startupMs = -1;	// how long starting took, once known
};

#endif	  // APP_STATE_MACHINE_H
