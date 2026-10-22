#include "QuestionStateMachine.h"

#include <algorithm>
#include <optional>

#include "MachineArgs.h"

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

std::shared_ptr<QuestionStateMachine> QuestionStateMachine::instance()
{
	static const std::shared_ptr<QuestionStateMachine> machine = std::make_shared<QuestionStateMachine>();
	return machine;
}

int QuestionStateMachine::ask(
	const std::string& text, const std::vector<std::string>& options, QuestionPresenter* presenter)
{
	std::uint64_t id = 0;
	{
		Transaction transaction(*this);
		id = m_nextId++;
		m_open.push_back({id, text, options, presenter});
		if (m_open.size() == 1)
		{
			transaction.move(Event::Ask);
		}
	}

	presenter->presentQuestion(id, text, options);

	Transaction transaction(*this);
	m_answered.wait(transaction.lock(), [&]() { return m_answers.contains(id); });
	return m_answers.extract(id).mapped();
}

bool QuestionStateMachine::isOpen(std::uint64_t id) const
{
	const auto lock = this->lock();
	return std::ranges::contains(m_open, id, &Question::id);
}

bool QuestionStateMachine::answer(std::uint64_t id, int option, EventSource source)
{
	QuestionPresenter* presenter = nullptr;
	{
		Transaction transaction(*this);
		const auto it = std::ranges::find(m_open, id, &Question::id);
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
			transaction.move(Event::Answer, source);
			if (!m_open.empty())
			{
				transaction.move(Event::Ask);
			}
		}
	}
	m_answered.notify_all();
	if (presenter != nullptr)
	{
		presenter->dismissQuestion(id);
	}
	return true;
}

void QuestionStateMachine::withdraw(QuestionPresenter* presenter)
{
	std::vector<Question> withdrawn;
	{
		Transaction transaction(*this);
		const bool currentWithdrawn = !m_open.empty() &&
			(presenter == nullptr || m_open.front().presenter == presenter);
		for (auto it = m_open.begin(); it != m_open.end();)
		{
			if (presenter == nullptr || it->presenter == presenter)
			{
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
			transaction.move(Event::Withdraw);
			if (!m_open.empty())
			{
				transaction.move(Event::Ask);
			}
		}
	}
	m_answered.notify_all();
	for (const Question& question: withdrawn)
	{
		if (question.presenter != nullptr)
		{
			question.presenter->dismissQuestion(question.id);
		}
	}
}

MachineFireResult QuestionStateMachine::fire(const std::string& event, const std::string& args, EventSource source)
{
	if (controlNamed(event) != Event::Answer)
	{
		return unknownEvent(event);
	}
	const auto parsed = parseArgs<question_machine_json::AnswerArgs>(
		event, args, R"({"option": n} or {"label": "..."})");
	if (!parsed)
	{
		return {false, parsed.error()};
	}

	std::uint64_t id = 0;
	int option = -1;
	{
		const auto lock = this->lock();
		if (m_open.empty())
		{
			return {false, "no question is open"};
		}
		auto question = m_open.begin();
		if (parsed->id)
		{
			question = std::ranges::find(m_open, *parsed->id, &Question::id);
			if (question == m_open.end())
			{
				return {false, std::format("question {} is not open", *parsed->id)};
			}
		}
		id = question->id;
		if (parsed->label)
		{
			const auto it = std::ranges::find(question->options, *parsed->label);
			if (it == question->options.end())
			{
				return {false, std::format("'{}' is not one of the options", *parsed->label)};
			}
			option = static_cast<int>(it - question->options.begin());
		}
		else if (parsed->option)
		{
			option = *parsed->option;
			if (option < 0 || option >= static_cast<int>(question->options.size()))
			{
				return {false, std::format("option {} is out of range", option)};
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

std::string QuestionStateMachine::contextLocked() const
{
	if (m_open.empty())
	{
		return "{}";
	}
	const Question& question = m_open.front();
	const question_machine_json::QuestionContext context {
		question.id, question.text, question.options, m_open.size() - 1};
	return glz::write_json(context).value_or("{}");
}
