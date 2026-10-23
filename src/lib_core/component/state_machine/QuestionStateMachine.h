#ifndef QUESTION_STATE_MACHINE_H
#define QUESTION_STATE_MACHINE_H

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ConcurrentStateMachine.h"
#include "StdexecPrelude.h"

// Shows a question to the user. The answer comes back through
// QuestionStateMachine::answer, from the user's click or from an agent.
class QuestionPresenter
{
public:
	virtual ~QuestionPresenter() = default;

	// May block (a modal loop on the GUI thread) until the question is answered; from
	// another thread it shows the question and returns.
	virtual void presentQuestion(
		std::uint64_t id, const std::string& text, const std::vector<std::string>& options) = 0;
	// Question `id` was answered elsewhere or withdrawn: close it if it is still shown.
	virtual void dismissQuestion(std::uint64_t id) = 0;
};

// The questions the application asks (DialogView::confirm, DialogView::asked):
// "reindex the project?", "keep the data of an interrupted run?". Remote control
// sees the open question and its options, and answers it like a click would.
// Asking holds no thread: the answer goes to the asker's callback, or completes
// the asker's sender; only the blocking `ask` waits for it.
//
//   idle --ask--> asking --answer/withdraw--> idle
//
// Questions asked while one is open queue up behind it: answering the first moves
// to idle, and `ask` brings up the next.
struct QuestionMachine
{
	enum class State
	{
		Idle,
		Asking
	};
	enum class Event
	{
		Ask,
		Answer,
		Withdraw
	};

	struct Question
	{
		std::uint64_t id = 0;
		std::string text;
		std::vector<std::string> options;
		QuestionPresenter* presenter = nullptr;
		std::function<void(int)> answered;	// the asker's, called once
	};

	struct Data
	{
		std::deque<Question> open;	// front: the question the state is about
		std::uint64_t nextId = 1;
	};

	static constexpr std::string_view name = "question";
	static constexpr State initial = State::Idle;
	static constexpr std::array states {named(State::Idle, "idle"), named(State::Asking, "asking")};
	static constexpr std::array events {
		named(Event::Ask, "ask"), control(Event::Answer, "answer"), named(Event::Withdraw, "withdraw")};
	static constexpr auto transitions = std::to_array<StateMachine<State, Event>::Transition>({
		{State::Idle, Event::Ask, State::Asking},
		{State::Asking, Event::Answer, State::Idle},
		{State::Asking, Event::Withdraw, State::Idle},
	});

	static std::string context(State state, const Data& data);
};

class QuestionSender;

class QuestionStateMachine: public std::enable_shared_from_this<QuestionStateMachine>
{
public:
	using State = QuestionMachine::State;

	static std::shared_ptr<QuestionStateMachine> instance();

	// The answer as a sender: the index of the chosen option, or -1 when the question
	// was withdrawn or closed without a choice. Stopping the sender withdraws it.
	// Without a presenter nobody can answer: -1 at once.
	QuestionSender asked(std::string text, std::vector<std::string> options, QuestionPresenter* presenter);

	// Asks and waits for the answer (see asked).
	int ask(const std::string& text, const std::vector<std::string>& options, QuestionPresenter* presenter);

	// Opens a question without showing it: `answered` gets the answer once, on the
	// thread that answers or withdraws it. Returns its id, for present and withdraw.
	std::uint64_t open(
		std::string text,
		std::vector<std::string> options,
		QuestionPresenter* presenter,
		std::function<void(int)> answered);

	// Shows open question `id` (its presenter may block until it is answered).
	void present(std::uint64_t id);

	// Whether question `id` still waits for its answer.
	bool isOpen(std::uint64_t id) const;

	// Answers open question `id`. False when it is not open (any more).
	bool answer(std::uint64_t id, int option, EventSource source);

	// Withdraws the open questions `presenter` shows (all for nullptr): their askers
	// get -1. For a window that closed, or a presenter going away.
	void withdraw(QuestionPresenter* presenter);

	// Withdraws question `id` if it is still open: its asker gets -1.
	void withdraw(std::uint64_t id);

	MachineSnapshot snapshot() const
	{
		return m_machine.snapshot();
	}
	MachineFireResult fire(const std::string& event, const std::string& args, EventSource source);

private:
	using Event = QuestionMachine::Event;
	using Question = QuestionMachine::Question;

	// Withdraws the open questions `which` matches: their askers get -1.
	void withdrawWhere(const std::function<bool(const Question&)>& which);

	ConcurrentStateMachine<QuestionMachine> m_machine;
};

// QuestionStateMachine::asked: a question as a sender of its answer.
class QuestionSender
{
public:
	using sender_concept = stdexec::sender_t;
	using completion_signatures = stdexec::completion_signatures<stdexec::set_value_t(int)>;

	QuestionSender(
		std::shared_ptr<QuestionStateMachine> machine,
		std::string text,
		std::vector<std::string> options,
		QuestionPresenter* presenter)
		: m_machine(std::move(machine))
		, m_text(std::move(text))
		, m_options(std::move(options))
		, m_presenter(presenter)
	{
	}

	template <class Receiver>
	struct Operation
	{
		struct OnStop
		{
			Operation* self;
			void operator()() noexcept
			{
				self->machine->withdraw(self->id);
			}
		};
		using StopToken = stdexec::stop_token_of_t<stdexec::env_of_t<Receiver>>;

		Receiver receiver;
		std::shared_ptr<QuestionStateMachine> machine;
		std::string text;
		std::vector<std::string> options;
		QuestionPresenter* presenter;
		std::uint64_t id = 0;
		int answer = -1;
		std::optional<typename StopToken::template callback_type<OnStop>> onStop;
		// The answer can come while start is still setting up (a box shown inline on
		// the GUI thread, a stop meanwhile): whichever of the two comes second completes.
		enum class Phase
		{
			Starting,
			Waiting,
			Answered
		};
		std::atomic<Phase> phase {Phase::Starting};

		void start() & noexcept
		{
			const StopToken token = stdexec::get_stop_token(stdexec::get_env(receiver));
			if (presenter == nullptr || token.stop_requested())
			{
				stdexec::set_value(std::move(receiver), -1);
				return;
			}
			id = machine->open(std::move(text), std::move(options), presenter, [this](int option) {
				answer = option;
				if (phase.exchange(Phase::Answered) == Phase::Waiting)
				{
					complete();
				}
			});
			onStop.emplace(token, OnStop {this});
			machine->present(id);
			if (phase.exchange(Phase::Waiting) == Phase::Answered)
			{
				complete();
			}
		}

		void complete() noexcept
		{
			onStop.reset();
			stdexec::set_value(std::move(receiver), answer);
		}
	};

	template <class Receiver>
	Operation<std::decay_t<Receiver>> connect(Receiver&& receiver) && noexcept
	{
		return {std::forward<Receiver>(receiver), std::move(m_machine), std::move(m_text), std::move(m_options), m_presenter};
	}

private:
	std::shared_ptr<QuestionStateMachine> m_machine;
	std::string m_text;
	std::vector<std::string> m_options;
	QuestionPresenter* m_presenter;
};

#endif	  // QUESTION_STATE_MACHINE_H
