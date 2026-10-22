#ifndef PROJECT_STATE_MACHINE_H
#define PROJECT_STATE_MACHINE_H

#include <chrono>
#include <memory>
#include <string>

#include "AppStateMachine.h"
#include "ConcurrentStateMachine.h"
#include "RefreshInfo.h"

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
struct ProjectMachine
{
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

	struct Data
	{
		std::string path;	// the project loading or open
		std::string outcome;	// how the last load or close ended
		bool waitingForStartup = false;
		std::chrono::milliseconds openingLimit {10000};
	};

	static constexpr std::string_view name = "project";
	static constexpr State initial = State::NoProject;
	static constexpr std::array states {
		named(State::NoProject, "no_project"),
		named(State::Loading, "loading"),
		named(State::Opening, "opening"),
		named(State::Ready, "ready"),
		named(State::Unloading, "unloading")};
	static constexpr std::array events {
		control(Event::Load, "load"),
		control(Event::Close, "close"),
		named(Event::Requested, "requested"),
		named(Event::Kept, "kept"),
		named(Event::Failed, "failed"),
		named(Event::Loaded, "loaded"),
		named(Event::TabOpening, "tab_opening"),
		named(Event::Reloading, "reloading"),
		named(Event::Shown, "shown"),
		named(Event::TimedOut, "timed_out"),
		named(Event::Closed, "closed"),
		named(Event::CloseRefused, "close_refused")};
	static constexpr auto transitions = std::to_array<StateMachine<State, Event>::Transition>({
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
	});

	static std::string context(State state, const Data& data);
};

class ProjectStateMachine
{
public:
	using State = ProjectMachine::State;

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

	MachineSnapshot snapshot() const
	{
		return m_machine.snapshot();
	}
	MachineFireResult fire(const std::string& event, const std::string& args, EventSource source);

private:
	using Event = ProjectMachine::Event;

	// Moves along an application event, recording `outcome` when given.
	void report(Event event, const std::string* outcome = nullptr);

	ConcurrentStateMachine<ProjectMachine> m_machine;
	const std::shared_ptr<AppStateMachine> m_app;
};

#endif	  // PROJECT_STATE_MACHINE_H
