#ifndef QUESTION_STATE_MACHINE_H
#define QUESTION_STATE_MACHINE_H

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "RemoteStateMachine.h"
#include "StateMachine.h"

// Shows a question to the user. The answer comes back through
// QuestionStateMachine::answer, from the user's click or from an agent.
class QuestionPresenter
{
public:
	virtual ~QuestionPresenter() = default;

	// May block (a modal loop on the GUI thread) until the question is answered.
	virtual void presentQuestion(
		std::uint64_t id, const std::string& text, const std::vector<std::string>& options) = 0;
	// Question `id` was answered elsewhere or withdrawn: close it if it is still shown.
	virtual void dismissQuestion(std::uint64_t id) = 0;
};

// The questions the application asks and waits on (DialogView::confirm): "reindex
// the project?", "keep the data of an interrupted run?". Remote control sees the
// open question and its options, and answers it like a click would.
//
//   idle --ask--> asking --answer/withdraw--> idle
//
// Questions asked while one is open queue up behind it: answering the first moves
// to idle, and `ask` brings up the next.
class QuestionStateMachine: public RemoteStateMachine
{
public:
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

	static std::shared_ptr<QuestionStateMachine> instance();

	QuestionStateMachine();

	// Asks and waits for the answer: the index of the chosen option, or -1 when the
	// question was withdrawn or closed without a choice.
	int ask(const std::string& text, const std::vector<std::string>& options, QuestionPresenter* presenter);

	// Whether question `id` still waits for its answer.
	bool isOpen(std::uint64_t id) const;

	// Answers open question `id`. False when it is not open (any more).
	bool answer(std::uint64_t id, int option, EventSource source);

	// Withdraws the open questions `presenter` shows (all for nullptr): their askers
	// get -1. For a window that closed, or a presenter going away.
	void withdraw(QuestionPresenter* presenter);

	std::string name() const override;
	MachineSnapshot snapshot() const override;
	MachineFireResult fire(const std::string& event, const std::string& args, EventSource source) override;

private:
	struct Question
	{
		std::uint64_t id = 0;
		std::string text;
		std::vector<std::string> options;
		QuestionPresenter* presenter = nullptr;
	};

	// Under m_mutex: moves the machine and records the transition to publish.
	void move(Event event, EventSource source, std::vector<MachineTransition>& published);
	std::string contextLocked() const;
	MachineSnapshot snapshotLocked() const;

	mutable std::mutex m_mutex;
	std::condition_variable m_answered;
	StateMachine<State, Event> m_machine;
	std::deque<Question> m_open;	// front: the question the state is about
	std::map<std::uint64_t, int> m_answers;	// answered, not yet collected by the asker
	std::uint64_t m_nextId = 1;
};

#endif	  // QUESTION_STATE_MACHINE_H
