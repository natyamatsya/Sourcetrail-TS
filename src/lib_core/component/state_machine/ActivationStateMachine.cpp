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
	Ticket(Machine::Weak machine, std::uint64_t generation, std::string view)
		: m_machine(std::move(machine)), m_generation(generation), m_view(std::move(view))
	{
	}
	Ticket(const Ticket&) = delete;
	Ticket& operator=(const Ticket&) = delete;
	~Ticket()
	{
		if (const std::optional<Machine> machine = m_machine.lock())
		{
			auto transaction = machine->transaction();
			drawn(transaction, m_generation, m_view);
		}
	}

private:
	const Machine::Weak m_machine;
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
	observer->follows = [](const MessageBase& message) { return changesViews(message.getType()); };
	observer->started = [weak = m_machine.weak()](const MessageBase&) {
		if (const std::optional<Machine> machine = weak.lock())
		{
			auto transaction = machine->transaction();
			started(transaction);
		}
	};
	observer->handled = [weak = m_machine.weak()](const MessageBase&) {
		if (const std::optional<Machine> machine = weak.lock())
		{
			auto transaction = machine->transaction();
			handled(transaction);
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
	auto transaction = m_machine.transaction();
	started(transaction);
}

void ActivationStateMachine::messageHandled()
{
	auto transaction = m_machine.transaction();
	handled(transaction);
}

std::shared_ptr<void> ActivationStateMachine::drawing(const std::string& view)
{
	auto transaction = m_machine.transaction();
	if (transaction.state() == State::Idle)
	{
		return {};
	}
	transaction.data().drawing[view]++;
	return std::make_shared<Ticket>(m_machine.weak(), transaction.data().generation, view);
}

bool ActivationStateMachine::isIdle() const
{
	return m_machine.state() == State::Idle;
}

void ActivationStateMachine::setRenderingLimit(std::chrono::milliseconds limit)
{
	m_machine.transaction().data().renderingLimit = limit;
}

void ActivationStateMachine::started(Machine::Transaction& transaction)
{
	ActivationMachine::Data& data = transaction.data();
	data.inFlight++;
	if (transaction.state() != State::Activating)
	{
		data.requestedAt = std::chrono::steady_clock::now();
		data.outcome.clear();
		data.activation++;
		transaction.move(Event::Requested);
	}
}

void ActivationStateMachine::handled(Machine::Transaction& transaction)
{
	if (transaction.data().inFlight > 0)
	{
		transaction.data().inFlight--;
	}
	advance(transaction);
}

void ActivationStateMachine::drawn(Machine::Transaction& transaction, std::uint64_t generation, const std::string& view)
{
	ActivationMachine::Data& data = transaction.data();
	if (generation != data.generation)
	{
		return;	// a rendering that timed out stopped waiting for it
	}
	if (const auto it = data.drawing.find(view); it != data.drawing.end() && --it->second <= 0)
	{
		data.drawing.erase(it);
	}
	advance(transaction);
}

namespace
{
std::size_t ticketsHeld(const ActivationMachine::Data& data)
{
	std::size_t count = 0;
	for (const auto& [view, tickets]: data.drawing)
	{
		count += static_cast<std::size_t>(tickets);
	}
	return count;
}

std::int64_t msSince(std::chrono::steady_clock::time_point start)
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}
}	 // namespace

void ActivationStateMachine::advance(Machine::Transaction& transaction)
{
	ActivationMachine::Data& data = transaction.data();
	switch (transaction.state())
	{
	case State::Activating:
		if (data.inFlight > 0)
		{
			return;
		}
		if (ticketsHeld(data) > 0)
		{
			transaction.move(Event::Handled);
			// Gives up on views that do not report in time.
			transaction.timeout(data.renderingLimit, [](Machine::Transaction& timedOut) {
				ActivationMachine::Data& timedOutData = timedOut.data();
				std::string views;
				for (const auto& [view, tickets]: timedOutData.drawing)
				{
					views += (views.empty() ? "" : ", ") + view;
				}
				timedOutData.outcome = std::format("timed out waiting for {} to draw", views);
				timedOut.defer(
					[warning = std::format("activation state machine: {}; moving on to idle", timedOutData.outcome)]() {
						LOG_WARNING(warning);
					});
				timedOutData.drawing.clear();
				timedOutData.generation++;
				timedOutData.tookMs = msSince(timedOutData.requestedAt);
				timedOut.move(Event::TimedOut);
			});
			return;
		}
		break;
	case State::Rendering:
		if (ticketsHeld(data) > 0)
		{
			return;
		}
		break;
	case State::Idle:
		return;
	}
	data.tookMs = msSince(data.requestedAt);
	transaction.move(Event::Shown);
}

std::string ActivationMachine::context(State  /*state*/, const Data& data)
{
	activation_machine_json::ActivationContext context;
	context.activation = data.activation;
	if (!data.drawing.empty())
	{
		// Not `| std::ranges::to<...>()`: clang cannot instantiate that adaptor from libstdc++ 14.
		const auto drawing = data.drawing | std::views::keys;
		context.drawing = std::vector<std::string>(drawing.begin(), drawing.end());
	}
	if (data.tookMs >= 0)
	{
		context.took_ms = data.tookMs;
	}
	if (!data.outcome.empty())
	{
		context.outcome = data.outcome;
	}
	return glz::write_json(context).value_or("{}");
}
