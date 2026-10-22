#ifndef REMOTE_STATE_MACHINE_H
#define REMOTE_STATE_MACHINE_H

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Who sent an event: the person at the GUI, an agent through agent control, or the
// application itself (the indexing pipeline moving on). Every transition says, so a
// remote observer can tell its own moves from the user's.
enum class EventSource
{
	Ui,
	Agent,
	App
};

const char* eventSourceName(EventSource source);

// A machine as remote control sees it.
struct MachineSnapshot
{
	std::string machine;
	std::string state;
	std::vector<std::string> events;	// what the machine accepts now from outside
	std::string context;	// a JSON object: what the state is about (a question, progress, ...)
	std::uint64_t revision = 0;
};

struct MachineTransition
{
	std::string machine;
	std::string from;
	std::string to;
	std::string event;
	EventSource source = EventSource::App;
	std::string context;	// after the transition
	std::uint64_t revision = 0;
};

struct MachineFireResult
{
	bool ok = false;
	std::string message;
};

// A state machine that can be read and driven by name: the seam remote control
// uses instead of finding and pressing widgets. A dialog that shows one of these
// states is a view of it; its buttons send the same events an agent does.
class RemoteStateMachine
{
public:
	virtual ~RemoteStateMachine() = default;

	virtual std::string name() const = 0;
	virtual MachineSnapshot snapshot() const = 0;

	// Sends `event` with `args` (a JSON object; empty for none) from `source`. Fails,
	// changing nothing, when the machine does not accept the event now or the args
	// do not fit it.
	virtual MachineFireResult fire(
		const std::string& event, const std::string& args, EventSource source) = 0;
};

// The machines remote control can reach, and the observers of their transitions.
class StateMachineRegistry
{
public:
	using Observer = std::function<void(const MachineTransition&)>;

	static StateMachineRegistry& instance();

	void add(std::shared_ptr<RemoteStateMachine> machine);
	std::shared_ptr<RemoteStateMachine> find(const std::string& name) const;
	std::vector<std::shared_ptr<RemoteStateMachine>> all() const;

	std::size_t addObserver(Observer observer);
	void removeObserver(std::size_t id);

	// Called by a machine after it moved, outside its own lock, on whichever thread
	// sent the event.
	void publish(const MachineTransition& transition) const;

private:
	mutable std::mutex m_mutex;
	std::vector<std::shared_ptr<RemoteStateMachine>> m_machines;
	std::map<std::size_t, Observer> m_observers;
	std::size_t m_nextObserverId = 1;
};

#endif	  // REMOTE_STATE_MACHINE_H
