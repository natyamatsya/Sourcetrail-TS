#ifndef REMOTE_STATE_MACHINE_H
#define REMOTE_STATE_MACHINE_H

#include <concepts>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
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
	std::uint64_t cause = 0;	// the request it was on behalf of (MessageCause.h), 0 for none
};

struct MachineFireResult
{
	bool ok = false;
	std::string message;
};

// A state machine that can be read and driven by name: the seam remote control
// uses instead of finding and pressing widgets. A dialog that shows one of these
// states is a view of it; its buttons send the same events an agent does.
//
// It has a snapshot and, when it takes events from outside, `fire(event, args,
// source)`: sends `event` with `args` (a JSON object; empty for none) from `source`,
// failing, and changing nothing, when the machine does not accept the event now or
// the args do not fit it. A machine without `fire` is watched, not driven.
template <typename Machine>
concept RemoteMachine = requires(const Machine& machine) {
	{ machine.snapshot() } -> std::same_as<MachineSnapshot>;
};

template <typename Machine>
concept DrivenRemoteMachine = RemoteMachine<Machine> &&
	requires(Machine& machine, const std::string& text, EventSource source) {
		{ machine.fire(text, text, source) } -> std::same_as<MachineFireResult>;
	};

// Any RemoteMachine, shared: what the registry keeps. A value; the machine's type is
// erased behind a table of functions made for it.
class AnyStateMachine
{
public:
	template <RemoteMachine Machine>
	explicit AnyStateMachine(std::shared_ptr<Machine> machine)
		: m_name(machine->snapshot().machine), m_machine(std::move(machine)), m_operations(&operationsOf<Machine>())
	{
	}

	const std::string& name() const
	{
		return m_name;
	}

	MachineSnapshot snapshot() const
	{
		return m_operations->snapshot(m_machine.get());
	}

	MachineFireResult fire(const std::string& event, const std::string& args, EventSource source) const
	{
		return m_operations->fire(m_machine.get(), m_name, event, args, source);
	}

private:
	struct Operations
	{
		MachineSnapshot (*snapshot)(const void* machine);
		MachineFireResult (*fire)(
			void* machine, const std::string& name, const std::string& event, const std::string& args, EventSource source);
	};

	template <RemoteMachine Machine>
	static MachineSnapshot snapshotOf(const void* machine)
	{
		return static_cast<const Machine*>(machine)->snapshot();
	}

	template <RemoteMachine Machine>
	static MachineFireResult fireOf(
		[[maybe_unused]] void* machine,
		[[maybe_unused]] const std::string& name,
		const std::string& event,
		[[maybe_unused]] const std::string& args,
		[[maybe_unused]] EventSource source)
	{
		if constexpr (DrivenRemoteMachine<Machine>)
		{
			return static_cast<Machine*>(machine)->fire(event, args, source);
		}
		else
		{
			return {false, "the " + name + " machine takes no events from outside ('" + event + "'): watch it"};
		}
	}

	// The table for `Machine`, one per type.
	template <RemoteMachine Machine>
	static const Operations& operationsOf()
	{
		static constexpr Operations operations {&snapshotOf<Machine>, &fireOf<Machine>};
		return operations;
	}

	std::string m_name;
	std::shared_ptr<void> m_machine;
	const Operations* m_operations;
};

// The machines remote control can reach, and the observers of their transitions.
class StateMachineRegistry
{
public:
	using Observer = std::function<void(const MachineTransition&)>;

	static StateMachineRegistry& instance();

	// Keeps `machine` unless one of its name is known already.
	void add(AnyStateMachine machine);
	template <RemoteMachine Machine>
	void add(std::shared_ptr<Machine> machine)
	{
		add(AnyStateMachine(std::move(machine)));
	}
	std::optional<AnyStateMachine> find(const std::string& name) const;
	std::vector<AnyStateMachine> all() const;

	std::size_t addObserver(Observer observer);
	void removeObserver(std::size_t id);

	// Called by a machine after it moved, outside its own lock, on whichever thread
	// sent the event.
	void publish(const MachineTransition& transition) const;

private:
	mutable std::mutex m_mutex;
	std::vector<AnyStateMachine> m_machines;
	std::map<std::size_t, Observer> m_observers;
	std::size_t m_nextObserverId = 1;
};

#endif	  // REMOTE_STATE_MACHINE_H
