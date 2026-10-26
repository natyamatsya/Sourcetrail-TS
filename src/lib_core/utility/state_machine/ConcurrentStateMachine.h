#ifndef CONCURRENT_STATE_MACHINE_H
#define CONCURRENT_STATE_MACHINE_H

#include <algorithm>
#include <array>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "MessageCause.h"
#include "RemoteStateMachine.h"
#include "StateMachine.h"
#include "WorkPools.h"

// A state or an event with its name, as remote control spells it. `control`: an
// event sent from outside (a dialog's buttons, an agent); the others come from the
// application.
template <typename Value>
struct Named
{
	Value value;
	std::string_view name;
	bool control = false;
};

template <typename Value>
constexpr Named<Value> named(Value value, std::string_view name)
{
	return {value, name, false};
}

template <typename Event>
constexpr Named<Event> control(Event event, std::string_view name)
{
	return {event, name, true};
}

// What a machine is, as data: its states and events (scoped enums), their names, the
// state it starts in and its transition table. Optionally the data it keeps beside
// the table (`Data`, guarded with it) and what a state is about, as a JSON object
// (`static std::string context(State, const Data&)`).
//
//   struct LightMachine
//   {
//       enum class State { Off, On };
//       enum class Event { Switch };
//       struct Data { int switches = 0; };
//       static constexpr std::string_view name = "light";
//       static constexpr State initial = State::Off;
//       static constexpr std::array states {named(State::Off, "off"), named(State::On, "on")};
//       static constexpr std::array events {control(Event::Switch, "switch")};
//       static constexpr std::array transitions {...};   // StateMachine<State, Event>::Transition
//       static std::string context(State state, const Data& data);
//   };
template <typename Spec>
concept MachineSpec = requires {
	typename Spec::State;
	typename Spec::Event;
	requires std::is_scoped_enum_v<typename Spec::State> && std::is_scoped_enum_v<typename Spec::Event>;
	{ Spec::name } -> std::convertible_to<std::string_view>;
	{ Spec::initial } -> std::convertible_to<typename Spec::State>;
	requires std::same_as<std::ranges::range_value_t<decltype(Spec::states)>, Named<typename Spec::State>>;
	requires std::same_as<std::ranges::range_value_t<decltype(Spec::events)>, Named<typename Spec::Event>>;
	requires std::same_as<
		std::ranges::range_value_t<decltype(Spec::transitions)>,
		typename StateMachine<typename Spec::State, typename Spec::Event>::Transition>;
};

// The states a spec's machine rests in between episodes (Spec::resting), by default
// its initial state. The application's own moves in an episode that come without a
// cause (from another thread: the GUI drawing, the indexing pipeline) are on behalf
// of the cause of the episode's last move from outside; a person's move without one
// takes the episode over.
template <typename Spec>
constexpr bool isResting(typename Spec::State state)
{
	if constexpr (requires { Spec::resting; })
	{
		return std::ranges::find(Spec::resting, state) != std::ranges::end(Spec::resting);
	}
	else
	{
		return state == Spec::initial;
	}
}

// The data a spec keeps beside its table: Spec::Data, or nothing.
template <typename Spec>
struct SpecDataOf
{
	struct None
	{
	};
	using type = None;
};

template <typename Spec>
	requires requires { typename Spec::Data; }
struct SpecDataOf<Spec>
{
	using type = typename Spec::Data;
};

template <typename Spec>
using SpecData = typename SpecDataOf<Spec>::type;

template <typename Spec>
concept MachineSpecWithContext = MachineSpec<Spec> &&
	requires(typename Spec::State state, const SpecData<Spec>& data) {
		{ Spec::context(state, data) } -> std::convertible_to<std::string>;
	};

// Checked at compile time: every state and event the table uses has exactly one name,
// the names are distinct, and no state has two rows for one event.
template <MachineSpec Spec>
consteval bool isWellFormed()
{
	const auto namedOnce = [](const auto& names, auto value) {
		return std::ranges::count_if(names, [&](const auto& n) { return n.value == value; }) == 1;
	};
	const auto distinctNames = [](const auto& names) {
		return std::ranges::all_of(names, [&](const auto& n) {
			return std::ranges::count_if(names, [&](const auto& m) { return m.name == n.name; }) == 1;
		});
	};
	if (!namedOnce(Spec::states, Spec::initial) || !distinctNames(Spec::states) || !distinctNames(Spec::events))
	{
		return false;
	}
	return std::ranges::all_of(Spec::transitions, [&](const auto& t) {
		return namedOnce(Spec::states, t.from) && namedOnce(Spec::states, t.to) &&
			namedOnce(Spec::events, t.event) &&
			std::ranges::count_if(Spec::transitions, [&](const auto& u) {
				return u.from == t.from && u.event == t.event;
			}) == 1;
	});
}

// A MachineSpec made safe to drive from any thread: a handle to a monitor that owns
// the table and the spec's Data behind one lock. The data is reachable only through
// a Transaction (or read()), so it is never touched without the lock.
//
// The moves made in a Transaction are published once it ends, after the lock is
// released (an observer may read the machine back), in the order made; then the
// work it deferred runs.
//
// One timeout at a time: armed in a Transaction, it lasts until the machine moves,
// and its handler runs in a Transaction of its own on the work pool, when the shared
// timer says (execution::afterDelay): no machine keeps a thread for its timeouts. It
// works on the machine's data only, so it never outlives the machine.
//
// A machine is a member of the class that gives it its operations; that class
// meets RemoteMachine (snapshot, fire) for remote control. Copies of the handle share
// the machine; callbacks that may outlive it keep a Weak one.
template <MachineSpec Spec>
class ConcurrentStateMachine
{
	static_assert(isWellFormed<Spec>(), "a state machine spec names its states and events once and has one row per (state, event)");

	struct Monitor;

public:
	using State = typename Spec::State;
	using Event = typename Spec::Event;
	using Data = SpecData<Spec>;

	// Holds the machine's lock: read and change its state and data. The moves are
	// published when the Transaction ends, after the lock is released.
	class Transaction
	{
	public:
		explicit Transaction(const ConcurrentStateMachine& machine)
			: Transaction(machine.m_monitor, std::unique_lock<std::mutex>(machine.m_monitor->mutex))
		{
		}
		Transaction(const Transaction&) = delete;
		Transaction& operator=(const Transaction&) = delete;

		~Transaction()
		{
			if (m_lock.owns_lock())
			{
				m_lock.unlock();
			}
			for (const MachineTransition& transition: m_published)
			{
				StateMachineRegistry::instance().publish(transition);
			}
			for (const std::function<void()>& work: m_deferred)
			{
				work();
			}
		}

		Data& data()
		{
			return m_monitor->data;
		}

		const Data& data() const
		{
			return m_monitor->data;
		}

		State state() const
		{
			return m_monitor->table.state();
		}

		std::uint64_t revision() const
		{
			return m_monitor->table.revision();
		}

		bool accepts(Event event) const
		{
			return m_monitor->table.accepts(event);
		}

		// Moves along `event`, ending a running timeout. False, and no change, when the
		// state has no row for it. The move is on behalf of the thread's cause; an
		// application move without one, of the episode's (isResting).
		bool move(Event event, EventSource source = EventSource::App)
		{
			const State from = state();
			if (!m_monitor->table.fire(event))
			{
				return false;
			}
			m_monitor->timeout.reset();
			if (const std::uint64_t cause = currentCause(); cause != 0 || source != EventSource::App)
			{
				m_monitor->episodeCause = cause;
			}
			m_published.push_back(
				{std::string(Spec::name),
				 std::string(stateName(from)),
				 std::string(stateName(state())),
				 std::string(eventName(event)),
				 source,
				 m_monitor->context(),
				 revision(),
				 m_monitor->episodeCause});
			if (isResting<Spec>(state()))
			{
				m_monitor->episodeCause = 0;
			}
			return true;
		}

		// The answer to a control the state does not accept.
		MachineFireResult refuse(Event event) const
		{
			return {false, std::format("'{}' is not accepted in state '{}'", eventName(event), stateName(state()))};
		}

		// After `limit`, unless the machine moved meanwhile, `onTimeout` runs in a
		// Transaction of its own.
		void timeout(std::chrono::milliseconds limit, std::function<void(Transaction&)> onTimeout)
		{
			const auto deadline = std::chrono::steady_clock::now() + limit;
			m_monitor->timeout = Timeout {revision(), deadline, std::move(onTimeout)};
			execution::afterDelay(limit, [weak = std::weak_ptr<Monitor>(m_monitor), deadline]() {
				execution::runOnWorkPool([weak, deadline]() {
					if (const std::shared_ptr<Monitor> monitor = weak.lock())
					{
						Monitor::expire(monitor, deadline);
					}
				});
			});
		}

		// Runs `work` once the Transaction has ended, after its moves are published:
		// what must not run under the lock (logging, which may dispatch messages).
		void defer(std::function<void()> work)
		{
			m_deferred.push_back(std::move(work));
		}

		// For a condition variable to wait on.
		std::unique_lock<std::mutex>& lock()
		{
			return m_lock;
		}

	private:
		friend struct Monitor;

		Transaction(std::shared_ptr<Monitor> monitor, std::unique_lock<std::mutex> lock)
			: m_monitor(std::move(monitor)), m_lock(std::move(lock))
		{
		}

		std::shared_ptr<Monitor> m_monitor;	// alive until the moves are published
		std::unique_lock<std::mutex> m_lock;
		std::vector<MachineTransition> m_published;
		std::vector<std::function<void()>> m_deferred;
	};

	// A handle that does not keep the machine alive: for callbacks that may outlive it.
	class Weak
	{
	public:
		std::optional<ConcurrentStateMachine> lock() const
		{
			if (std::shared_ptr<Monitor> monitor = m_monitor.lock())
			{
				return ConcurrentStateMachine(std::move(monitor));
			}
			return std::nullopt;
		}

	private:
		friend class ConcurrentStateMachine;

		explicit Weak(std::weak_ptr<Monitor> monitor): m_monitor(std::move(monitor)) {}

		std::weak_ptr<Monitor> m_monitor;
	};

	ConcurrentStateMachine(): m_monitor(std::make_shared<Monitor>())
	{
		m_monitor->self = m_monitor;
	}

	Transaction transaction() const
	{
		return Transaction(*this);
	}

	// Calls `reader(data, state)` under the lock and returns what it returns.
	template <std::invocable<const Data&, State> Reader>
	auto read(Reader&& reader) const
	{
		const std::lock_guard<std::mutex> lock(m_monitor->mutex);
		return std::invoke(std::forward<Reader>(reader), std::as_const(m_monitor->data), m_monitor->table.state());
	}

	State state() const
	{
		return read([](const Data&, State state) { return state; });
	}

	// The state, the controls it accepts, the context and the revision.
	MachineSnapshot snapshot() const
	{
		const std::lock_guard<std::mutex> lock(m_monitor->mutex);
		MachineSnapshot snapshot;
		snapshot.machine = std::string(Spec::name);
		snapshot.state = stateName(m_monitor->table.state());
		for (const Event event: m_monitor->table.acceptedEvents())
		{
			if (isControl(event))
			{
				snapshot.events.emplace_back(eventName(event));
			}
		}
		snapshot.context = m_monitor->context();
		snapshot.revision = m_monitor->table.revision();
		return snapshot;
	}

	Weak weak() const
	{
		return Weak(m_monitor);
	}

	static constexpr std::string_view stateName(State state)
	{
		return nameOf(Spec::states, state);
	}

	static constexpr std::string_view eventName(Event event)
	{
		return nameOf(Spec::events, event);
	}

	static constexpr bool isControl(Event event)
	{
		const auto it = std::ranges::find(Spec::events, event, &Named<Event>::value);
		return it != std::ranges::end(Spec::events) && it->control;
	}

	// The control called `name`, if there is one.
	static constexpr std::optional<Event> controlNamed(std::string_view name)
	{
		const auto it = std::ranges::find(Spec::events, name, &Named<Event>::name);
		if (it == std::ranges::end(Spec::events) || !it->control)
		{
			return std::nullopt;
		}
		return it->value;
	}

	// "refresh, select_mode, ...": the controls, in the order the spec names them.
	static std::string controlNames()
	{
		std::string names;
		for (const Named<Event>& event: Spec::events)
		{
			if (event.control)
			{
				names += (names.empty() ? "" : ", ") + std::string(event.name);
			}
		}
		return names;
	}

	static MachineFireResult unknownEvent(std::string_view event)
	{
		return {
			false,
			std::format("unknown event '{}' ({}; the others come from the application)", event, controlNames())};
	}

private:
	struct Timeout
	{
		std::uint64_t revision = 0;
		std::chrono::steady_clock::time_point deadline;
		std::function<void(Transaction&)> onTimeout;
	};

	struct Monitor
	{
		mutable std::mutex mutex;
		StateMachine<State, Event> table {
			Spec::initial, {std::ranges::begin(Spec::transitions), std::ranges::end(Spec::transitions)}};
		Data data {};
		std::uint64_t episodeCause = 0;	// what set the machine going from rest
		std::weak_ptr<Monitor> self;
		std::optional<Timeout> timeout;

		// What the current state is about, as a JSON object. The lock is held.
		std::string context() const
		{
			if constexpr (MachineSpecWithContext<Spec>)
			{
				return Spec::context(table.state(), data);
			}
			else
			{
				return "{}";
			}
		}

		// The timeout due at `deadline` is due: its handler runs, unless the machine
		// moved since (which ended it) or it was armed anew (a later call is that one's).
		static void expire(const std::shared_ptr<Monitor>& monitor, std::chrono::steady_clock::time_point deadline)
		{
			std::unique_lock<std::mutex> lock(monitor->mutex);
			if (!monitor->timeout || monitor->timeout->deadline != deadline ||
				monitor->table.revision() != monitor->timeout->revision)
			{
				return;
			}
			Timeout due = std::move(*monitor->timeout);
			monitor->timeout.reset();
			Transaction transaction(monitor, std::move(lock));
			due.onTimeout(transaction);
		}
	};

	explicit ConcurrentStateMachine(std::shared_ptr<Monitor> monitor): m_monitor(std::move(monitor)) {}

	template <typename Names, typename Value>
	static constexpr std::string_view nameOf(const Names& names, Value value)
	{
		const auto it = std::ranges::find(names, value, &Named<Value>::value);
		return it == std::ranges::end(names) ? std::string_view("?") : it->name;
	}

	std::shared_ptr<Monitor> m_monitor;
};

#endif	  // CONCURRENT_STATE_MACHINE_H
