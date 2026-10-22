#ifndef PROJECT_STATE_MACHINE_H
#define PROJECT_STATE_MACHINE_H

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "AppStateMachine.h"
#include "RefreshInfo.h"
#include "RemoteStateMachine.h"
#include "StateMachine.h"

// The open project's lifecycle, from a load request until the project is usable:
//
//   no_project/ready --load--> loading --loaded--> ready
//   loading --tab_opening--> opening --shown/timed_out--> ready
//   loading --kept--> ready, loading --failed--> no_project
//   ready --reloading--> loading (an indexing run ended: its index loads)
//   ready --close--> unloading --closed--> no_project (--close_refused--> ready)
//
// A load ends once the project is open and, when it opens a tab (the overview of
// a project with an index), that tab has shown its first activation: nothing of
// the load lands after a first navigation. Application and TabsController report
// the steps; `load` and `close` come from agents. A load asked for while the app
// is still starting waits for it (AppStateMachine::whenStarted).
class ProjectStateMachine
	: public RemoteStateMachine
	, public std::enable_shared_from_this<ProjectStateMachine>
{
public:
	enum class State
	{
		NoProject,
		Loading,
		Opening,
		Ready,
		Unloading
	};
	enum class Event
	{
		// Sent by agents.
		Load,
		Close,
		// Sent by the application.
		Requested,
		Kept,
		Failed,
		Loaded,
		TabOpening,
		Reloading,
		Shown,
		TimedOut,
		Closed,
		CloseRefused
	};

	static std::shared_ptr<ProjectStateMachine> instance();

	explicit ProjectStateMachine(std::shared_ptr<AppStateMachine> app);

	// The controls.

	// Loads the project at `path` (refreshing it with `refresh` once open).
	MachineFireResult load(const std::string& path, RefreshMode refresh, EventSource source);
	MachineFireResult close(EventSource source);

	// The application's side.

	// Application starts loading `path`: from the menu, the command line, a recent
	// project, or `load`.
	void requested(const std::string& path);
	// The load left the open project as it was (the same project, or refused while
	// indexing): `outcome` says which.
	void kept(const std::string& outcome);
	void failed(const std::string& outcome);
	// The project is open and no tab is coming (it has no index yet, or has tabs).
	void loaded();
	// An indexing run ended: the new index loads, and TabsController decides
	// whether the overview opens (as after a load).
	void reloading();
	// The project's first tab opens; its first activation ends the load.
	void tabOpening();
	void shown();
	void closed();
	void closeRefused(const std::string& outcome);

	// How long `opening` waits for the tab's first activation before it moves on
	// (with a warning): a tab that never activates must not hold the app.
	void setOpeningLimit(std::chrono::milliseconds limit);

	std::string name() const override;
	MachineSnapshot snapshot() const override;
	MachineFireResult fire(const std::string& event, const std::string& args, EventSource source) override;

	static const char* stateName(State state);
	static const char* eventName(Event event);

private:
	bool move(Event event, EventSource source, std::vector<MachineTransition>& published);
	void moveAndPublish(Event event, const std::string* outcome = nullptr);
	void openingTimedOut(std::uint64_t revision);
	std::string contextLocked() const;

	const std::shared_ptr<AppStateMachine> m_app;
	mutable std::mutex m_mutex;
	StateMachine<State, Event> m_machine;
	std::string m_path;	// the project loading or open
	std::string m_outcome;	// how the last load or close ended
	bool m_waitingForStartup = false;
	std::chrono::milliseconds m_openingLimit {10000};
};

#endif	  // PROJECT_STATE_MACHINE_H
