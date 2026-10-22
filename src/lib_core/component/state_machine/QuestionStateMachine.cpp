#include "QuestionStateMachine.h"

#include <algorithm>
#include <optional>

#include <glaze/glaze.hpp>

// glaze reflects these for JSON; MSVC cannot reflect types in an anonymous namespace.
namespace question_machine_json
{
struct QuestionContext
{
	std::uint64_t id = 0;
	std::string text;
	std::vector<std::string> options;
	std::size_t queued = 0;	// further questions waiting behind this one
};

struct AnswerArgs
{
	std::optional<int> option;	// the index of the option
	std::optional<std::string> label;	// or its text
	std::optional<std::uint64_t> id;	// default: the question the state is about
};
}	 // namespace question_machine_json

namespace
{
using namespace question_machine_json;

using State = QuestionStateMachine::State;
using Event = QuestionStateMachine::Event;

const char* stateName(State state)
{
	return state == State::Asking ? "asking" : "idle";
}

const char* eventName(Event event)
{
	switch (event)
	{
	case Event::Ask:
		return "ask";
	case Event::Answer:
		return "answer";
	case Event::Withdraw:
		break;
	}
	return "withdraw";
}
}	 // namespace

std::shared_ptr<QuestionStateMachine> QuestionStateMachine::instance()
{
	static const std::shared_ptr<QuestionStateMachine> machine = std::make_shared<QuestionStateMachine>();
	return machine;
}

QuestionStateMachine::QuestionStateMachine()
	: m_machine(
		  State::Idle,
		  {
			  {State::Idle, Event::Ask, State::Asking},
			  {State::Asking, Event::Answer, State::Idle},
			  {State::Asking, Event::Withdraw, State::Idle},
		  })
{
}

int QuestionStateMachine::ask(
	const std::string& text, const std::vector<std::string>& options, QuestionPresenter* presenter)
{
	std::uint64_t id = 0;
	std::vector<MachineTransition> published;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		id = m_nextId++;
		m_open.push_back({id, text, options, presenter});
		if (m_open.size() == 1)
		{
			move(Event::Ask, EventSource::App, published);
		}
	}
	for (const MachineTransition& transition: published)
	{
		StateMachineRegistry::instance().publish(transition);
	}

	presenter->presentQuestion(id, text, options);

	std::unique_lock<std::mutex> lock(m_mutex);
	m_answered.wait(lock, [&]() { return m_answers.contains(id); });
	const int option = m_answers[id];
	m_answers.erase(id);
	return option;
}

bool QuestionStateMachine::isOpen(std::uint64_t id) const
{
	const std::lock_guard<std::mutex> lock(m_mutex);
	return std::any_of(m_open.begin(), m_open.end(), [id](const Question& q) { return q.id == id; });
}

bool QuestionStateMachine::answer(std::uint64_t id, int option, EventSource source)
{
	QuestionPresenter* presenter = nullptr;
	std::vector<MachineTransition> published;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		auto it = std::find_if(m_open.begin(), m_open.end(), [id](const Question& q) { return q.id == id; });
		if (it == m_open.end())
		{
			return false;
		}
		const bool current = it == m_open.begin();
		presenter = it->presenter;
		m_open.erase(it);
		m_answers[id] = option;
		if (current)
		{
			move(Event::Answer, source, published);
			if (!m_open.empty())
			{
				move(Event::Ask, EventSource::App, published);
			}
		}
	}
	m_answered.notify_all();
	for (const MachineTransition& transition: published)
	{
		StateMachineRegistry::instance().publish(transition);
	}
	if (presenter != nullptr)
	{
		presenter->dismissQuestion(id);
	}
	return true;
}

void QuestionStateMachine::withdraw(QuestionPresenter* presenter)
{
	std::vector<Question> withdrawn;
	std::vector<MachineTransition> published;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		bool currentWithdrawn = false;
		for (auto it = m_open.begin(); it != m_open.end();)
		{
			if (presenter == nullptr || it->presenter == presenter)
			{
				currentWithdrawn = currentWithdrawn || it == m_open.begin();
				m_answers[it->id] = -1;
				withdrawn.push_back(*it);
				it = m_open.erase(it);
			}
			else
			{
				++it;
			}
		}
		if (currentWithdrawn)
		{
			move(Event::Withdraw, EventSource::App, published);
			if (!m_open.empty())
			{
				move(Event::Ask, EventSource::App, published);
			}
		}
	}
	m_answered.notify_all();
	for (const MachineTransition& transition: published)
	{
		StateMachineRegistry::instance().publish(transition);
	}
	for (const Question& question: withdrawn)
	{
		if (question.presenter != nullptr)
		{
			question.presenter->dismissQuestion(question.id);
		}
	}
}

std::string QuestionStateMachine::name() const
{
	return "question";
}

MachineSnapshot QuestionStateMachine::snapshot() const
{
	const std::lock_guard<std::mutex> lock(m_mutex);
	return snapshotLocked();
}

MachineFireResult QuestionStateMachine::fire(const std::string& event, const std::string& args, EventSource source)
{
	if (event != "answer")
	{
		return {false, "the question machine accepts 'answer' (ask and withdraw come from the application)"};
	}

	AnswerArgs parsed;
	if (const auto error = glz::read_json(parsed, args.empty() ? std::string("{}") : args))
	{
		return {false, "answer: " + glz::format_error(error, args) + R"( (expected {"option": n} or {"label": "..."}))"};
	}

	std::uint64_t id = 0;
	int option = -1;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		if (m_open.empty())
		{
			return {false, "no question is open"};
		}
		const Question* question = &m_open.front();
		if (parsed.id)
		{
			auto it = std::find_if(
				m_open.begin(), m_open.end(), [&](const Question& q) { return q.id == *parsed.id; });
			if (it == m_open.end())
			{
				return {false, "question " + std::to_string(*parsed.id) + " is not open"};
			}
			question = &*it;
		}
		id = question->id;
		if (parsed.label)
		{
			auto it = std::find(question->options.begin(), question->options.end(), *parsed.label);
			if (it == question->options.end())
			{
				return {false, "'" + *parsed.label + "' is not one of the options"};
			}
			option = static_cast<int>(it - question->options.begin());
		}
		else if (parsed.option)
		{
			option = *parsed.option;
			if (option < 0 || option >= static_cast<int>(question->options.size()))
			{
				return {false, "option " + std::to_string(option) + " is out of range"};
			}
		}
		else
		{
			return {false, R"(answer needs {"option": n} or {"label": "..."})"};
		}
	}

	if (!answer(id, option, source))
	{
		return {false, "the question was answered meanwhile"};
	}
	return {true, "answered"};
}

void QuestionStateMachine::move(Event event, EventSource source, std::vector<MachineTransition>& published)
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
		 source,
		 contextLocked(),
		 m_machine.revision()});
}

std::string QuestionStateMachine::contextLocked() const
{
	if (m_open.empty())
	{
		return "{}";
	}
	const Question& question = m_open.front();
	const QuestionContext context {question.id, question.text, question.options, m_open.size() - 1};
	return glz::write_json(context).value_or("{}");
}

MachineSnapshot QuestionStateMachine::snapshotLocked() const
{
	MachineSnapshot snapshot;
	snapshot.machine = name();
	snapshot.state = stateName(m_machine.state());
	if (m_machine.accepts(Event::Answer))
	{
		snapshot.events.push_back(eventName(Event::Answer));
	}
	snapshot.context = contextLocked();
	snapshot.revision = m_machine.revision();
	return snapshot;
}
