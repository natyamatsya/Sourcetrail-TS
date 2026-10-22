#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

// A transition table over enum states and events: the state changes only along a
// row (from, event) -> to, so what a machine can do next is data, not code paths,
// and "which events are accepted now" is a lookup. Not synchronized: the machine
// that owns one guards it. See context/DESIGN_AGENT_UI_CONTROL.md (State machines).
template <typename State, typename Event>
class StateMachine
{
public:
	struct Transition
	{
		State from;
		Event event;
		State to;
	};

	StateMachine(State initial, std::vector<Transition> table)
		: m_state(initial), m_table(std::move(table))
	{
	}

	State state() const
	{
		return m_state;
	}

	// Counts the moves made, so an observer can tell two visits of a state apart.
	std::uint64_t revision() const
	{
		return m_revision;
	}

	std::optional<State> target(Event event) const
	{
		for (const Transition& transition: m_table)
		{
			if (transition.from == m_state && transition.event == event)
			{
				return transition.to;
			}
		}
		return std::nullopt;
	}

	bool accepts(Event event) const
	{
		return target(event).has_value();
	}

	// The events the current state has a row for, in table order.
	std::vector<Event> acceptedEvents() const
	{
		std::vector<Event> events;
		for (const Transition& transition: m_table)
		{
			if (transition.from == m_state &&
				std::find(events.begin(), events.end(), transition.event) == events.end())
			{
				events.push_back(transition.event);
			}
		}
		return events;
	}

	// Moves along the row for `event`. False, and no change, when the current state
	// has none.
	bool fire(Event event)
	{
		const std::optional<State> to = target(event);
		if (!to)
		{
			return false;
		}
		m_state = *to;
		m_revision++;
		return true;
	}

private:
	State m_state;
	std::vector<Transition> m_table;
	std::uint64_t m_revision = 0;
};

#endif	  // STATE_MACHINE_H
