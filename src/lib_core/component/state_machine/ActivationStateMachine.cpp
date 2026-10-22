#include "ActivationStateMachine.h"

#include <optional>
#include <set>

#include <glaze/glaze.hpp>

#include "MessageBase.h"
#include "MessageQueue.h"
#include "logging.h"

// glaze reflects this for JSON; MSVC cannot reflect types in an anonymous namespace.
namespace activation_machine_json
{
struct ActivationContext
{
	std::uint64_t activation = 0;	// activations requested so far
	std::optional<std::vector<std::string>> drawing;	// the views still drawing
	std::optional<std::int64_t> took_ms;	// the last activation, request to shown
	std::optional<std::string> outcome;	// how the last one ended, when not as it should
};
}	 // namespace activation_machine_json

namespace
{
using State = ActivationStateMachine::State;
using Event = ActivationStateMachine::Event;

void publishAll(const std::vector<MachineTransition>& published)
{
	for (const MachineTransition& transition: published)
	{
		StateMachineRegistry::instance().publish(transition);
	}
}
}	 // namespace

// Reports its view drawn when the last copy goes.
class ActivationStateMachine::Ticket
{
public:
	Ticket(std::weak_ptr<ActivationStateMachine> machine, std::uint64_t generation, std::string view)
		: m_machine(std::move(machine)), m_generation(generation), m_view(std::move(view))
	{
	}
	Ticket(const Ticket&) = delete;
	Ticket& operator=(const Ticket&) = delete;
	~Ticket()
	{
		if (const auto machine = m_machine.lock())
		{
			machine->drawn(m_generation, m_view);
		}
	}

private:
	const std::weak_ptr<ActivationStateMachine> m_machine;
	const std::uint64_t m_generation;
	const std::string m_view;
};

std::shared_ptr<ActivationStateMachine> ActivationStateMachine::instance()
{
	static const std::shared_ptr<ActivationStateMachine> machine = std::make_shared<ActivationStateMachine>();
	return machine;
}

ActivationStateMachine::ActivationStateMachine()
	: m_machine(
		  State::Idle,
		  {
			  {State::Idle, Event::Requested, State::Activating},
			  {State::Activating, Event::Handled, State::Rendering},
			  {State::Activating, Event::Shown, State::Idle},
			  {State::Rendering, Event::Requested, State::Activating},
			  {State::Rendering, Event::Shown, State::Idle},
			  {State::Rendering, Event::TimedOut, State::Idle},
		  })
{
}

ActivationStateMachine::~ActivationStateMachine()
{
	if (m_watchdog.joinable())
	{
		m_watchdog.request_stop();
		m_watchdog.join();
	}
}

void ActivationStateMachine::followMessages()
{
	auto observer = std::make_shared<MessageQueue::Observer>();
	const std::weak_ptr<ActivationStateMachine> weak = weak_from_this();
	observer->follows = [](const MessageBase& message) { return changesViews(message.getType()); };
	observer->started = [weak](const MessageBase&) {
		if (const auto self = weak.lock())
		{
			self->messageStarted();
		}
	};
	observer->handled = [weak](const MessageBase&) {
		if (const auto self = weak.lock())
		{
			self->messageHandled();
		}
	};
	MessageQueue::getInstance()->setObserver(observer);
}

bool ActivationStateMachine::changesViews(const std::string& messageType)
{
	static const std::set<std::string> types = {
		// activations, and the history replaying them
		"MessageActivateBoundaries",
		"MessageActivateEdge",
		"MessageActivateErrors",
		"MessageActivateFile",
		"MessageActivateFullTextSearch",
		"MessageActivateLegend",
		"MessageActivateLocalSymbols",
		"MessageActivateNodes",
		"MessageActivateOverview",
		"MessageActivateSourceLocations",
		"MessageActivateTokenIds",
		"MessageActivateTokens",
		"MessageActivateTrail",
		"MessageActivateTrailEdge",
		"MessageBookmarkActivate",
		"MessageCustomTrailShow",
		"MessageDeactivateEdge",
		"MessageErrorsAll",
		"MessageErrorsForFile",
		"MessageFlushUpdates",
		"MessageHistoryRedo",
		"MessageHistoryToPosition",
		"MessageHistoryUndo",
		"MessageSearch",
		// the graph
		"MessageGraphNodeBundleSplit",
		"MessageGraphNodeExpand",
		"MessageGraphNodeHide",
		// the code view
		"MessageChangeFileView",
		"MessageCodeShowDefinition",
		"MessageScrollToLine",
		"MessageShowError",
		"MessageShowReference",
		"MessageShowScope",
		"MessageToNextCodeReference",
	};
	return types.contains(messageType);
}

void ActivationStateMachine::messageStarted()
{
	std::vector<MachineTransition> published;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_inFlight++;
		if (m_machine.state() != State::Activating)
		{
			m_requestedAt = std::chrono::steady_clock::now();
			m_outcome.clear();
			m_activation++;
			move(Event::Requested, published);
		}
	}
	publishAll(published);
}

void ActivationStateMachine::messageHandled()
{
	std::vector<MachineTransition> published;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		if (m_inFlight > 0)
		{
			m_inFlight--;
		}
		advanceLocked(published);
	}
	publishAll(published);
}

std::shared_ptr<void> ActivationStateMachine::drawing(const std::string& view)
{
	const std::lock_guard<std::mutex> lock(m_mutex);
	if (m_machine.state() == State::Idle)
	{
		return {};
	}
	m_drawing[view]++;
	return std::make_shared<Ticket>(weak_from_this(), m_generation, view);
}

bool ActivationStateMachine::isIdle() const
{
	const std::lock_guard<std::mutex> lock(m_mutex);
	return m_machine.state() == State::Idle;
}

void ActivationStateMachine::setRenderingLimit(std::chrono::milliseconds limit)
{
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_renderingLimit = limit;
	}
	m_wake.notify_all();
}

std::string ActivationStateMachine::name() const
{
	return "activation";
}

MachineSnapshot ActivationStateMachine::snapshot() const
{
	const std::lock_guard<std::mutex> lock(m_mutex);
	MachineSnapshot snapshot;
	snapshot.machine = name();
	snapshot.state = stateName(m_machine.state());
	snapshot.context = contextLocked();
	snapshot.revision = m_machine.revision();
	return snapshot;
}

MachineFireResult ActivationStateMachine::fire(const std::string& event, const std::string&  /*args*/, EventSource  /*source*/)
{
	return {
		false,
		"the activation machine takes no events from outside ('" + event +
			"'): it follows what the views show; activate through the nav_* tools"};
}

const char* ActivationStateMachine::stateName(State state)
{
	switch (state)
	{
	case State::Idle:
		return "idle";
	case State::Activating:
		return "activating";
	case State::Rendering:
		return "rendering";
	}
	return "?";
}

const char* ActivationStateMachine::eventName(Event event)
{
	switch (event)
	{
	case Event::Requested:
		return "requested";
	case Event::Handled:
		return "handled";
	case Event::Shown:
		return "shown";
	case Event::TimedOut:
		return "timed_out";
	}
	return "?";
}

void ActivationStateMachine::drawn(std::uint64_t generation, const std::string& view)
{
	std::vector<MachineTransition> published;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		if (generation != m_generation)
		{
			return;	// a rendering that timed out stopped waiting for it
		}
		if (const auto it = m_drawing.find(view); it != m_drawing.end() && --it->second <= 0)
		{
			m_drawing.erase(it);
		}
		advanceLocked(published);
	}
	publishAll(published);
}

void ActivationStateMachine::advanceLocked(std::vector<MachineTransition>& published)
{
	if (m_machine.state() == State::Activating)
	{
		if (m_inFlight > 0)
		{
			return;
		}
		if (drawingLocked() > 0)
		{
			m_renderingSince = std::chrono::steady_clock::now();
			move(Event::Handled, published);
			if (!m_watchdog.joinable())
			{
				m_watchdog = std::jthread([this](std::stop_token stop) { watch(stop); });
			}
			m_wake.notify_all();
			return;
		}
	}
	else if (m_machine.state() != State::Rendering || drawingLocked() > 0)
	{
		return;
	}
	m_tookMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::steady_clock::now() - m_requestedAt)
				   .count();
	move(Event::Shown, published);
	m_wake.notify_all();
}

void ActivationStateMachine::move(Event event, std::vector<MachineTransition>& published)
{
	const State from = m_machine.state();
	if (!m_machine.fire(event))
	{
		return;
	}
	published.push_back(
		{name(),
		 stateName(from),
		 stateName(m_machine.state()),
		 eventName(event),
		 EventSource::App,
		 contextLocked(),
		 m_machine.revision()});
}

std::size_t ActivationStateMachine::drawingLocked() const
{
	std::size_t count = 0;
	for (const auto& [view, tickets]: m_drawing)
	{
		count += static_cast<std::size_t>(tickets);
	}
	return count;
}

// The timer of `rendering`: moves on when the views have not reported in time.
void ActivationStateMachine::watch(std::stop_token stop)
{
	std::unique_lock<std::mutex> lock(m_mutex);
	while (!stop.stop_requested())
	{
		if (m_machine.state() != State::Rendering)
		{
			m_wake.wait(lock, stop, [this]() { return m_machine.state() == State::Rendering; });
			continue;
		}
		const std::uint64_t revision = m_machine.revision();
		const auto deadline = m_renderingSince + m_renderingLimit;
		if (m_wake.wait_until(lock, stop, deadline, [this, revision]() {
				return m_machine.revision() != revision ||
					std::chrono::steady_clock::now() >= m_renderingSince + m_renderingLimit;
			}) &&
			m_machine.revision() != revision)
		{
			continue;	// moved on, or a new rendering
		}
		if (stop.stop_requested() || m_machine.state() != State::Rendering ||
			std::chrono::steady_clock::now() < m_renderingSince + m_renderingLimit)
		{
			continue;
		}

		std::string views;
		for (const auto& [view, tickets]: m_drawing)
		{
			views += (views.empty() ? "" : ", ") + view;
		}
		m_outcome = "timed out waiting for " + views + " to draw";
		m_drawing.clear();
		m_generation++;
		m_tookMs = std::chrono::duration_cast<std::chrono::milliseconds>(
					   std::chrono::steady_clock::now() - m_requestedAt)
					   .count();
		std::vector<MachineTransition> published;
		move(Event::TimedOut, published);

		lock.unlock();
		LOG_WARNING("activation state machine: " + m_outcome + "; moving on to idle");
		publishAll(published);
		lock.lock();
	}
}

std::string ActivationStateMachine::contextLocked() const
{
	activation_machine_json::ActivationContext context;
	context.activation = m_activation;
	if (!m_drawing.empty())
	{
		std::vector<std::string> views;
		for (const auto& [view, tickets]: m_drawing)
		{
			views.push_back(view);
		}
		context.drawing = std::move(views);
	}
	if (m_tookMs >= 0)
	{
		context.took_ms = m_tookMs;
	}
	if (!m_outcome.empty())
	{
		context.outcome = m_outcome;
	}
	return glz::write_json(context).value_or("{}");
}
