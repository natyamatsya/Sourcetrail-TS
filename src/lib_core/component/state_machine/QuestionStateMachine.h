#ifndef QUESTION_STATE_MACHINE_H
#define QUESTION_STATE_MACHINE_H

#include <atomic>
#include <concepts>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ConcurrentStateMachine.h"
#include "MessageCause.h"
#include "Presenter.h"
#include "StdexecPrelude.h"

// Shows a question to the user. The answer comes back through
// QuestionStateMachine::answer, from the user's click or from an agent.
//
// presentQuestion(id, text, options) may block (a modal loop on the GUI thread)
// until the question is answered; from another thread it shows the question and
// returns. dismissQuestion(id): question `id` was answered elsewhere or withdrawn,
// close it if it is still shown.
template <typename T>
concept PresentsQuestions =
	requires(T& presenter, std::uint64_t id, const std::string& text, const std::vector<std::string>& options) {
		presenter.presentQuestion(id, text, options);
		presenter.dismissQuestion(id);
	};

struct QuestionPresenterOperations
{
	void (*present)(void* presenter, std::uint64_t id, const std::string& text, const std::vector<std::string>& options);
	void (*dismiss)(void* presenter, std::uint64_t id);
};

// Any PresentsQuestions, by reference (Presenter.h). Empty: nobody can answer.
class QuestionPresenter: public Presenter<QuestionPresenterOperations>
{
public:
	using Presenter::Presenter;

	template <PresentsQuestions T>
		requires(!std::derived_from<T, Presenter>)	// a handle is copied, not referred to
	QuestionPresenter(T& presenter): Presenter(presenter, &operations<T>)
	{
	}

	void presentQuestion(std::uint64_t id, const std::string& text, const std::vector<std::string>& options) const
	{
		m_operations->present(m_presenter, id, text, options);
	}

	void dismissQuestion(std::uint64_t id) const
	{
		m_operations->dismiss(m_presenter, id);
	}

private:
	template <PresentsQuestions T>
	static constexpr QuestionPresenterOperations operations {
		[](void* presenter, std::uint64_t id, const std::string& text, const std::vector<std::string>& options) {
			static_cast<T*>(presenter)->presentQuestion(id, text, options);
		},
		[](void* presenter, std::uint64_t id) { static_cast<T*>(presenter)->dismissQuestion(id); }};
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
		QuestionPresenter presenter;
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
	QuestionSender asked(std::string text, std::vector<std::string> options, QuestionPresenter presenter);

	// Asks and waits for the answer (see asked).
	int ask(const std::string& text, const std::vector<std::string>& options, QuestionPresenter presenter);

	// Opens a question without showing it: `answered` gets the answer once, on the
	// thread that answers or withdraws it. Returns its id, for present and withdraw.
	std::uint64_t open(
		std::string text,
		std::vector<std::string> options,
		QuestionPresenter presenter,
		std::function<void(int)> answered);

	// Shows open question `id` (its presenter may block until it is answered).
	void present(std::uint64_t id);

	// Whether question `id` still waits for its answer.
	bool isOpen(std::uint64_t id) const;

	// Answers open question `id`. False when it is not open (any more).
	bool answer(std::uint64_t id, int option, EventSource source);

	// Withdraws the open questions `presenter` shows (all for an empty one): their
	// askers get -1. For a window that closed, or a presenter going away.
	void withdraw(QuestionPresenter presenter);

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
		QuestionPresenter presenter)
		: m_machine(std::move(machine))
		, m_text(std::move(text))
		, m_options(std::move(options))
		, m_presenter(presenter)
		, m_cause(currentCause())
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
		QuestionPresenter presenter;
		std::uint64_t cause;	// asked on behalf of it, wherever the sender starts
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
			if (!presenter || token.stop_requested())
			{
				stdexec::set_value(std::move(receiver), -1);
				return;
			}
			const CauseScope scope(cause);
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
		return {
			std::forward<Receiver>(receiver),
			std::move(m_machine),
			std::move(m_text),
			std::move(m_options),
			m_presenter,
			m_cause};
	}

private:
	std::shared_ptr<QuestionStateMachine> m_machine;
	std::string m_text;
	std::vector<std::string> m_options;
	QuestionPresenter m_presenter;
	std::uint64_t m_cause;
};

// What an answer's callback threw, logged (QuestionStateMachine.cpp).
void reportAnswerFailure(const char* what);

// Asks `question` and goes on: `answered(option)` runs once it is answered (-1 for
// none), on the thread that answers it, on behalf of what asked; the question counts
// as that request's work until then (CauseWork).
template <std::invocable<int> Answered>
void whenAnswered(QuestionSender question, Answered answered)
{
	stdexec::start_detached(
		std::move(question) |
		stdexec::then([answered = std::move(answered), work = CauseWork::Held()](int option) mutable noexcept {
			const CauseScope scope(work.cause());
			try
			{
				answered(option);
			}
			catch (const std::exception& e)
			{
				reportAnswerFailure(e.what());
			}
			catch (...)
			{
				reportAnswerFailure("an unknown exception");
			}
		}));
}

#endif	  // QUESTION_STATE_MACHINE_H
