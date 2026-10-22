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
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "RemoteStateMachine.h"
#include "StateMachine.h"

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
// state it starts in and its transition table.
//
//   struct LightMachine
//   {
//       enum class State { Off, On };
//       enum class Event { Switch };
//       static constexpr std::string_view name = "light";
//       static constexpr State initial = State::Off;
//       static constexpr std::array states {named(State::Off, "off"), named(State::On, "on")};
//       static constexpr std::array events {control(Event::Switch, "switch")};
//       static constexpr std::array transitions {...};   // StateMachine<State, Event>::Transition
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

// A RemoteStateMachine over a MachineSpec, safe to drive from any thread.
//
// One lock guards the table and whatever the machine keeps beside it; a Transaction
// holds it. The moves made in a Transaction are published once it ends, after the
// lock is released (an observer may call back into the machine), in the order made.
//
// A machine may run one timeout at a time: armed in a Transaction, it lasts until the
// machine moves, and its handler runs in a Transaction of its own. Machines with a
// timeout are owned by a shared_ptr: the handler never runs on one that is going.
template <MachineSpec Spec>
class ConcurrentStateMachine
	: public RemoteStateMachine
	, public std::enable_shared_from_this<ConcurrentStateMachine<Spec>>
{
	static_assert(isWellFormed<Spec>(), "a state machine spec names its states and events once and has one row per (state, event)");

public:
	using State = typename Spec::State;
	using Event = typename Spec::Event;

	~ConcurrentStateMachine() override
	{
		if (m_timer.joinable())
		{
			m_timer.request_stop();
			// The last owner may be the timer's own handler: it returns without
			// touching the machine once stopped.
			if (m_timer.get_id() == std::this_thread::get_id())
			{
				m_timer.detach();
			}
			else
			{
				m_timer.join();
			}
		}
	}

	std::string name() const final
	{
		return std::string(Spec::name);
	}

	// The state, the controls it accepts, the context and the revision.
	MachineSnapshot snapshot() const final
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		MachineSnapshot snapshot;
		snapshot.machine = name();
		snapshot.state = stateName(m_machine.state());
		for (const Event event: m_machine.acceptedEvents())
		{
			if (isControl(event))
			{
				snapshot.events.emplace_back(eventName(event));
			}
		}
		snapshot.context = contextLocked();
		snapshot.revision = m_machine.revision();
		return snapshot;
	}

	State state() const
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		return m_machine.state();
	}

	// A machine with controls overrides this; one without is watched, not driven.
	MachineFireResult fire(const std::string& event, const std::string&  /*args*/, EventSource  /*source*/) override
	{
		if (controlNames().empty())
		{
			return {false, std::format("the {} machine takes no events from outside ('{}'): watch it", Spec::name, event)};
		}
		return unknownEvent(event);
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

protected:
	// Holds the machine's lock: read its state and data, move it. The moves are
	// published when the Transaction ends, after the lock is released.
	class Transaction
	{
	public:
		explicit Transaction(ConcurrentStateMachine& machine)
			: m_machine(machine), m_lock(machine.m_mutex)
		{
		}
		Transaction(ConcurrentStateMachine& machine, std::unique_lock<std::mutex> lock)
			: m_machine(machine), m_lock(std::move(lock))
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

		State state() const
		{
			return m_machine.m_machine.state();
		}

		std::uint64_t revision() const
		{
			return m_machine.m_machine.revision();
		}

		bool accepts(Event event) const
		{
			return m_machine.m_machine.accepts(event);
		}

		// Moves along `event`, ending a running timeout. False, and no change, when the
		// state has no row for it.
		bool move(Event event, EventSource source = EventSource::App)
		{
			const State from = m_machine.m_machine.state();
			if (!m_machine.m_machine.fire(event))
			{
				return false;
			}
			m_machine.m_timeout.reset();
			m_published.push_back(
				{std::string(Spec::name),
				 std::string(stateName(from)),
				 std::string(stateName(m_machine.m_machine.state())),
				 std::string(eventName(event)),
				 source,
				 m_machine.contextLocked(),
				 m_machine.m_machine.revision()});
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
			m_machine.m_timeout = Timeout {
				m_machine.m_machine.revision(), std::chrono::steady_clock::now() + limit, std::move(onTimeout)};
			if (!m_machine.m_timer.joinable())
			{
				m_machine.m_timer = std::jthread([&machine = m_machine](std::stop_token stop) { machine.watch(stop); });
			}
			m_machine.m_wake.notify_all();
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
		ConcurrentStateMachine& m_machine;
		std::unique_lock<std::mutex> m_lock;
		std::vector<MachineTransition> m_published;
		std::vector<std::function<void()>> m_deferred;
	};

	ConcurrentStateMachine()
		: m_machine(Spec::initial, {std::ranges::begin(Spec::transitions), std::ranges::end(Spec::transitions)})
	{
	}

	// Reading the machine's data without moving it.
	[[nodiscard]] std::unique_lock<std::mutex> lock() const
	{
		return std::unique_lock<std::mutex>(m_mutex);
	}

	// What the current state is about, as a JSON object. Called with the lock held.
	virtual std::string contextLocked() const
	{
		return "{}";
	}

	// For contextLocked: the lock is held.
	State stateLocked() const
	{
		return m_machine.state();
	}

	MachineFireResult unknownEvent(std::string_view event) const
	{
		return {
			false,
			std::format("unknown event '{}' ({}; the others come from the application)", event, controlNames())};
	}

	// A weak reference to the machine as its own type (empty unless a shared_ptr owns it).
	template <typename Self>
	std::weak_ptr<Self> weakSelf(this Self& self)
	{
		return std::static_pointer_cast<Self>(self.weak_from_this().lock());
	}

private:
	struct Timeout
	{
		std::uint64_t revision = 0;
		std::chrono::steady_clock::time_point deadline;
		std::function<void(Transaction&)> onTimeout;
	};

	template <typename Names, typename Value>
	static constexpr std::string_view nameOf(const Names& names, Value value)
	{
		const auto it = std::ranges::find(names, value, &Named<Value>::value);
		return it == std::ranges::end(names) ? std::string_view("?") : it->name;
	}

	void watch(std::stop_token stop)
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		while (!stop.stop_requested())
		{
			if (!m_timeout)
			{
				m_wake.wait(lock, stop, [this]() { return m_timeout.has_value(); });
				continue;
			}
			const auto deadline = m_timeout->deadline;
			if (m_wake.wait_until(lock, stop, deadline, [&]() { return !m_timeout || m_timeout->deadline != deadline; }) ||
				stop.stop_requested())
			{
				continue;	// moved, re-armed, or stopping
			}
			Timeout timeout = std::move(*m_timeout);
			m_timeout.reset();
			if (m_machine.revision() != timeout.revision)
			{
				continue;
			}
			std::shared_ptr<ConcurrentStateMachine> self = this->weak_from_this().lock();
			if (!self)
			{
				continue;	// not shared, or going
			}
			{
				Transaction transaction(*this, std::move(lock));
				timeout.onTimeout(transaction);
			}
			// `self` may hold the last reference: once stopped, `this` is gone.
			self.reset();
			if (stop.stop_requested())
			{
				return;
			}
			lock = std::unique_lock<std::mutex>(m_mutex);
		}
	}

	mutable std::mutex m_mutex;
	StateMachine<State, Event> m_machine;
	std::condition_variable_any m_wake;	// the timer: armed, ended or due
	std::optional<Timeout> m_timeout;
	std::jthread m_timer;	// started with the first timeout
};

#endif	  // CONCURRENT_STATE_MACHINE_H
