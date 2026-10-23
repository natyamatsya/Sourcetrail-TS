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

QuestionSender QuestionStateMachine::asked(
	std::string text, std::vector<std::string> options, QuestionPresenter* presenter)
{
	// Owned by its sender where a shared_ptr owns it (instance()); a machine of a
	// scope of its own outlives what it asks.
	std::shared_ptr<QuestionStateMachine> self = weak_from_this().lock();
	if (!self)
	{
		self = std::shared_ptr<QuestionStateMachine>(std::shared_ptr<void>(), this);
	}
	return QuestionSender(std::move(self), std::move(text), std::move(options), presenter);
}

int QuestionStateMachine::ask(
	const std::string& text, const std::vector<std::string>& options, QuestionPresenter* presenter)
{
	const auto answer = stdexec::sync_wait(asked(text, options, presenter));
	return answer ? std::get<0>(*answer) : -1;
}

std::uint64_t QuestionStateMachine::open(
	std::string text,
	std::vector<std::string> options,
	QuestionPresenter* presenter,
	std::function<void(int)> answered)
{
	auto transaction = m_machine.transaction();
	QuestionMachine::Data& data = transaction.data();
	const std::uint64_t id = data.nextId++;
	data.open.push_back({id, std::move(text), std::move(options), presenter, std::move(answered)});
	if (data.open.size() == 1)
	{
		transaction.move(Event::Ask);
	}
	return id;
}

void QuestionStateMachine::present(std::uint64_t id)
{
	const auto question = m_machine.read([id](const QuestionMachine::Data& data, State) -> std::optional<Question> {
		const auto it = std::ranges::find(data.open, id, &Question::id);
		return it != data.open.end() ? std::optional(*it) : std::nullopt;
	});
	if (question && question->presenter != nullptr)
	{
		question->presenter->presentQuestion(id, question->text, question->options);
	}
}

bool QuestionStateMachine::isOpen(std::uint64_t id) const
{
	return m_machine.read([id](const QuestionMachine::Data& data, State) {
		return std::ranges::contains(data.open, id, &Question::id);
	});
}

bool QuestionStateMachine::answer(std::uint64_t id, int option, EventSource source)
{
	QuestionPresenter* presenter = nullptr;
	std::function<void(int)> answered;
	{
		auto transaction = m_machine.transaction();
		QuestionMachine::Data& data = transaction.data();
		const auto it = std::ranges::find(data.open, id, &Question::id);
		if (it == data.open.end())
		{
			return false;
		}
		const bool current = it == data.open.begin();
		presenter = it->presenter;
		answered = std::move(it->answered);
		data.open.erase(it);
		if (current)
		{
			transaction.move(Event::Answer, source);
			if (!data.open.empty())
			{
				transaction.move(Event::Ask);
			}
		}
	}
	if (presenter != nullptr)
	{
		presenter->dismissQuestion(id);
	}
	if (answered)
	{
		answered(option);
	}
	return true;
}

void QuestionStateMachine::withdraw(QuestionPresenter* presenter)
{
	withdrawWhere([presenter](const Question& question) {
		return presenter == nullptr || question.presenter == presenter;
	});
}

void QuestionStateMachine::withdraw(std::uint64_t id)
{
	withdrawWhere([id](const Question& question) { return question.id == id; });
}

void QuestionStateMachine::withdrawWhere(const std::function<bool(const Question&)>& which)
{
	std::vector<Question> withdrawn;
	{
		auto transaction = m_machine.transaction();
		QuestionMachine::Data& data = transaction.data();
		const bool currentWithdrawn = !data.open.empty() && which(data.open.front());
		for (auto it = data.open.begin(); it != data.open.end();)
		{
			if (which(*it))
			{
				withdrawn.push_back(std::move(*it));
				it = data.open.erase(it);
			}
			else
			{
				++it;
			}
		}
		if (currentWithdrawn)
		{
			transaction.move(Event::Withdraw);
			if (!data.open.empty())
			{
				transaction.move(Event::Ask);
			}
		}
	}
	for (const Question& question: withdrawn)
	{
		if (question.presenter != nullptr)
		{
			question.presenter->dismissQuestion(question.id);
		}
		if (question.answered)
		{
			question.answered(-1);
		}
	}
}

MachineFireResult QuestionStateMachine::fire(const std::string& event, const std::string& args, EventSource source)
{
	if (m_machine.controlNamed(event) != Event::Answer)
	{
		return m_machine.unknownEvent(event);
	}
	const auto parsed = parseArgs<question_machine_json::AnswerArgs>(
		event, args, R"({"option": n} or {"label": "..."})");
	if (!parsed)
	{
		return {false, parsed.error()};
	}

	// Which question, and which of its options.
	const auto choice = m_machine.read(
		[&](const QuestionMachine::Data& data, State) -> std::expected<std::pair<std::uint64_t, int>, std::string> {
			if (data.open.empty())
			{
				return std::unexpected("no question is open");
			}
			auto question = data.open.begin();
			if (parsed->id)
			{
				question = std::ranges::find(data.open, *parsed->id, &Question::id);
				if (question == data.open.end())
				{
					return std::unexpected(std::format("question {} is not open", *parsed->id));
				}
			}
			if (parsed->label)
			{
				const auto it = std::ranges::find(question->options, *parsed->label);
				if (it == question->options.end())
				{
					return std::unexpected(std::format("'{}' is not one of the options", *parsed->label));
				}
				return std::pair(question->id, static_cast<int>(it - question->options.begin()));
			}
			if (parsed->option)
			{
				if (*parsed->option < 0 || *parsed->option >= static_cast<int>(question->options.size()))
				{
					return std::unexpected(std::format("option {} is out of range", *parsed->option));
				}
				return std::pair(question->id, *parsed->option);
			}
			return std::unexpected(R"(answer needs {"option": n} or {"label": "..."})");
		});
	if (!choice)
	{
		return {false, choice.error()};
	}
	if (!answer(choice->first, choice->second, source))
	{
		return {false, "the question was answered meanwhile"};
	}
	return {true, "answered"};
}

std::string QuestionMachine::context(State  /*state*/, const Data& data)
{
	if (data.open.empty())
	{
		return "{}";
	}
	const Question& question = data.open.front();
	const question_machine_json::QuestionContext context {
		question.id, question.text, question.options, data.open.size() - 1};
	return glz::write_json(context).value_or("{}");
}
