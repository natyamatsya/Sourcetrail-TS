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

void ActivationStateMachine::followMessages()
{
	auto observer = std::make_shared<MessageQueue::Observer>();
	const std::weak_ptr<ActivationStateMachine> weak = weakSelf();
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
	Transaction transaction(*this);
	m_inFlight++;
	if (transaction.state() != State::Activating)
	{
		m_requestedAt = std::chrono::steady_clock::now();
		m_outcome.clear();
		m_activation++;
		transaction.move(Event::Requested);
	}
}

void ActivationStateMachine::messageHandled()
{
	Transaction transaction(*this);
	if (m_inFlight > 0)
	{
		m_inFlight--;
	}
	advance(transaction);
}

std::shared_ptr<void> ActivationStateMachine::drawing(const std::string& view)
{
	const Transaction transaction(*this);
	if (transaction.state() == State::Idle)
	{
		return {};
	}
	m_drawing[view]++;
	return std::make_shared<Ticket>(weakSelf(), m_generation, view);
}

bool ActivationStateMachine::isIdle() const
{
	return state() == State::Idle;
}

void ActivationStateMachine::setRenderingLimit(std::chrono::milliseconds limit)
{
	const auto lock = this->lock();
	m_renderingLimit = limit;
}

void ActivationStateMachine::drawn(std::uint64_t generation, const std::string& view)
{
	Transaction transaction(*this);
	if (generation != m_generation)
	{
		return;	// a rendering that timed out stopped waiting for it
	}
	if (const auto it = m_drawing.find(view); it != m_drawing.end() && --it->second <= 0)
	{
		m_drawing.erase(it);
	}
	advance(transaction);
}

void ActivationStateMachine::advance(Transaction& transaction)
{
	switch (transaction.state())
	{
	case State::Activating:
		if (m_inFlight > 0)
		{
			return;
		}
		if (drawingLocked() > 0)
		{
			transaction.move(Event::Handled);
			// Gives up on views that do not report in time.
			transaction.timeout(m_renderingLimit, [this](Transaction& timedOut) {
				std::string views;
				for (const auto& [view, tickets]: m_drawing)
				{
					views += (views.empty() ? "" : ", ") + view;
				}
				m_outcome = std::format("timed out waiting for {} to draw", views);
				timedOut.defer([warning = std::format("activation state machine: {}; moving on to idle", m_outcome)]() {
					LOG_WARNING(warning);
				});
				m_drawing.clear();
				m_generation++;
				m_tookMs = std::chrono::duration_cast<std::chrono::milliseconds>(
							   std::chrono::steady_clock::now() - m_requestedAt)
							   .count();
				timedOut.move(Event::TimedOut);
			});
			return;
		}
		break;
	case State::Rendering:
		if (drawingLocked() > 0)
		{
			return;
		}
		break;
	case State::Idle:
		return;
	}
	m_tookMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::steady_clock::now() - m_requestedAt)
				   .count();
	transaction.move(Event::Shown);
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

std::string ActivationStateMachine::contextLocked() const
{
	activation_machine_json::ActivationContext context;
	context.activation = m_activation;
	if (!m_drawing.empty())
	{
		context.drawing = m_drawing | std::views::keys | std::ranges::to<std::vector<std::string>>();
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
