#ifndef ACTIVATION_STATE_MACHINE_H
#define ACTIVATION_STATE_MACHINE_H

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "ConcurrentStateMachine.h"

// What the graph and code views show, from a request until they have drawn it:
//
//   idle --requested--> activating --handled--> rendering --shown--> idle
//   activating --shown--> idle (nothing to draw), rendering --requested--> activating
//   rendering --timed_out--> idle (a view that never reported)
//
// An activation (a node, a file, the overview, a search, back/forward), and anything
// else that changes those views (expanding a node, a reference in the code view),
// is a message. `activating` lasts from its dispatch while such messages are
// handled: the controllers query the storage and lay the graph out, and what they
// dispatch on the way is part of the same activation (the message queue reports
// each one's dispatch and end, followMessages). Whoever dispatched one (a button an
// agent pressed) finds the machine activating already. The views then draw on the
// GUI thread, some of them animated: each holds a ticket (drawing) until it has,
// and `rendering` ends with the last.
struct ActivationMachine
{
	enum class State
	{
		Idle,
		Activating,
		Rendering
	};
	enum class Event
	{
		Requested,
		Handled,
		Shown,
		TimedOut
	};

	static constexpr std::string_view name = "activation";
	static constexpr State initial = State::Idle;
	static constexpr std::array states {
		named(State::Idle, "idle"), named(State::Activating, "activating"), named(State::Rendering, "rendering")};
	static constexpr std::array events {
		named(Event::Requested, "requested"),
		named(Event::Handled, "handled"),
		named(Event::Shown, "shown"),
		named(Event::TimedOut, "timed_out")};
	static constexpr auto transitions = std::to_array<StateMachine<State, Event>::Transition>({
		{State::Idle, Event::Requested, State::Activating},
		{State::Activating, Event::Handled, State::Rendering},
		{State::Activating, Event::Shown, State::Idle},
		{State::Rendering, Event::Requested, State::Activating},
		{State::Rendering, Event::Shown, State::Idle},
		{State::Rendering, Event::TimedOut, State::Idle},
	});
};

class ActivationStateMachine: public ConcurrentStateMachine<ActivationMachine>
{
public:
	static std::shared_ptr<ActivationStateMachine> instance();

	// Has the message queue report the messages that change the views (changesViews).
	void followMessages();
	static bool changesViews(const std::string& messageType);

	// The message side: one of those messages was dispatched, and has been handled
	// by all its listeners.
	void messageStarted();
	void messageHandled();

	// The views' side: `view` draws what an activation gave it and holds the
	// returned ticket until it has (an animation it starts included); dropping the
	// last copy reports it. Empty while idle: what draws then is no activation's.
	std::shared_ptr<void> drawing(const std::string& view);

	bool isIdle() const;

	// How long `rendering` waits for the views before it moves on (with a warning):
	// a view that never reports must not hold the app.
	void setRenderingLimit(std::chrono::milliseconds limit);

private:
	class Ticket;

	void drawn(std::uint64_t generation, const std::string& view);
	// Moves on what the counts allow: out of `activating` once no message is in
	// flight, out of `rendering` once nothing draws.
	void advance(Transaction& transaction);
	std::size_t drawingLocked() const;
	std::string contextLocked() const override;

	int m_inFlight = 0;	// messages that change the views, being handled
	std::map<std::string, int> m_drawing;	// tickets held, by view
	std::uint64_t m_generation = 0;	// a timed out rendering drops the tickets it waited for
	std::uint64_t m_activation = 0;	// activations requested so far
	std::chrono::steady_clock::time_point m_requestedAt;
	std::int64_t m_tookMs = -1;	// how long the last activation took, request to shown
	std::string m_outcome;
	std::chrono::milliseconds m_renderingLimit {10000};
};

#endif	  // ACTIVATION_STATE_MACHINE_H
