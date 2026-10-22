#ifndef ACTIVATION_STATE_MACHINE_H
#define ACTIVATION_STATE_MACHINE_H

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "RemoteStateMachine.h"
#include "StateMachine.h"

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
class ActivationStateMachine
	: public RemoteStateMachine
	, public std::enable_shared_from_this<ActivationStateMachine>
{
public:
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

	static std::shared_ptr<ActivationStateMachine> instance();

	ActivationStateMachine();
	~ActivationStateMachine() override;

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

	std::string name() const override;
	MachineSnapshot snapshot() const override;
	MachineFireResult fire(const std::string& event, const std::string& args, EventSource source) override;

	static const char* stateName(State state);
	static const char* eventName(Event event);

private:
	class Ticket;

	void drawn(std::uint64_t generation, const std::string& view);
	// Moves on what the counts allow: out of `activating` once no message is in
	// flight, out of `rendering` once nothing draws.
	void advanceLocked(std::vector<MachineTransition>& published);
	void move(Event event, std::vector<MachineTransition>& published);
	std::size_t drawingLocked() const;
	void watch(std::stop_token stop);
	std::string contextLocked() const;

	mutable std::mutex m_mutex;
	std::condition_variable_any m_wake;	// the watchdog: rendering started or ended
	StateMachine<State, Event> m_machine;
	int m_inFlight = 0;	// messages that change the views, being handled
	std::map<std::string, int> m_drawing;	// tickets held, by view
	std::uint64_t m_generation = 0;	// a timed out rendering drops the tickets it waited for
	std::uint64_t m_activation = 0;	// activations requested so far
	std::chrono::steady_clock::time_point m_requestedAt;
	std::chrono::steady_clock::time_point m_renderingSince;
	std::int64_t m_tookMs = -1;	// how long the last activation took, request to shown
	std::string m_outcome;
	std::chrono::milliseconds m_renderingLimit {10000};
	std::jthread m_watchdog;	// started with the first rendering
};

#endif	  // ACTIVATION_STATE_MACHINE_H
