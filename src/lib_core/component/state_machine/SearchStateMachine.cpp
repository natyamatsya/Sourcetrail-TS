#include "SearchStateMachine.h"

#include <algorithm>
#include <expected>
#include <format>
#include <optional>
#include <ranges>

#include "MachineArgs.h"
#include "MessageSearch.h"
#include "MessageSearchAutocomplete.h"

// glaze reflects these for JSON; MSVC cannot reflect types in an anonymous namespace.
namespace search_machine_json
{
struct Suggestion
{
	std::string name;
	std::string type;	// the node's kind ("class", "function", ...), empty for a command
	std::string kind;	// what it searches for: symbol, file, command, ...
};

struct SearchContext
{
	std::optional<std::vector<std::string>> tokens;	// what the box holds
	std::optional<std::string> query;
	std::optional<std::vector<Suggestion>> suggestions;
	std::optional<std::size_t> more;	// suggestions not listed
	std::optional<std::string> outcome;
};

struct TypeArgs
{
	std::string query;
};

struct SelectArgs
{
	std::optional<std::size_t> index;	// the suggestion's index
	std::optional<std::string> name;	// or its name
};
}	 // namespace search_machine_json

namespace
{
// How many suggestions the context lists.
constexpr std::size_t listedSuggestions = 30;

// The query a click on `match` runs in a box holding `tokens`: QtSmartSearchBox::addMatch keeps
// them when the last one is a filter, and replaces them otherwise (a previous search).
std::string queryWith(const std::vector<SearchMatch>& tokens, const SearchMatch& match)
{
	std::string query;
	if (!tokens.empty() && tokens.back().isFilterCommand())
	{
		for (const SearchMatch& token: tokens)
		{
			query += std::format("'{}' ", token.getFullName());
		}
	}
	return query + std::format("'{}'", match.getFullName());
}
}	 // namespace

std::shared_ptr<SearchStateMachine> SearchStateMachine::instance()
{
	static const std::shared_ptr<SearchStateMachine> machine = std::make_shared<SearchStateMachine>();
	return machine;
}

void SearchStateMachine::attach(SearchPresenter presenter)
{
	auto transaction = m_machine.transaction();
	transaction.data().presenter = presenter;
}

void SearchStateMachine::boxChanged(std::vector<SearchMatch> tokens, NodeTypeSet nodeTypes)
{
	auto transaction = m_machine.transaction();
	transaction.data().tokens = std::move(tokens);
	transaction.data().boxNodeTypes = nodeTypes;
}

void SearchStateMachine::detach(SearchPresenter presenter)
{
	auto transaction = m_machine.transaction();
	if (transaction.data().presenter == presenter)
	{
		transaction.data().presenter = {};
	}
}

MachineFireResult SearchStateMachine::type(const std::string& query, NodeTypeSet nodeTypes, EventSource source)
{
	if (query.empty())
	{
		return {false, "type needs a query"};
	}
	SearchPresenter presenter;
	{
		auto transaction = m_machine.transaction();
		SearchMachine::Data& data = transaction.data();
		data.query = query;
		data.nodeTypes = nodeTypes;
		data.matches.clear();
		data.outcome.clear();
		transaction.move(Event::Type, source);
		presenter = data.presenter;
	}
	if (presenter && source != EventSource::Ui)
	{
		presenter.presentQuery(query);
	}
	MessageSearchAutocomplete(query, nodeTypes).dispatch();
	return {true, std::format("completing '{}'", query)};
}

MachineFireResult SearchStateMachine::select(std::size_t index, EventSource source)
{
	SearchMatch match;
	NodeTypeSet nodeTypes;
	SearchPresenter presenter;
	std::string query;
	{
		auto transaction = m_machine.transaction();
		if (!transaction.accepts(Event::Select))
		{
			return transaction.refuse(Event::Select);
		}
		SearchMachine::Data& data = transaction.data();
		if (index >= data.matches.size())
		{
			return {false, std::format("suggestion {} is out of range ({} suggested)", index, data.matches.size())};
		}
		match = data.matches[index];
		nodeTypes = data.nodeTypes;
		presenter = data.presenter;
		// With the box, its filters stay, as on a click; without one, the suggestion alone.
		query = presenter ? queryWith(data.tokens, match) : std::format("'{}'", match.getFullName());
		data.outcome = std::format("searched for {}", query);
		data.matches.clear();
		transaction.move(Event::Select, source);
	}
	if (presenter)
	{
		presenter.selectSuggestion(match);
	}
	else
	{
		MessageSearch({match}, nodeTypes).dispatch();
	}
	return {true, std::format("searching for {}", query)};
}

MachineFireResult SearchStateMachine::dismiss(EventSource source)
{
	SearchPresenter presenter;
	{
		auto transaction = m_machine.transaction();
		if (!transaction.accepts(Event::Dismiss))
		{
			return transaction.refuse(Event::Dismiss);
		}
		SearchMachine::Data& data = transaction.data();
		data.outcome = "dismissed";
		data.matches.clear();
		transaction.move(Event::Dismiss, source);
		presenter = data.presenter;
	}
	if (presenter && source != EventSource::Ui)
	{
		presenter.dismissSuggestions();
	}
	return {true, "dismissed"};
}

MachineFireResult SearchStateMachine::search(EventSource source)
{
	SearchPresenter presenter;
	std::string query;
	{
		auto transaction = m_machine.transaction();
		SearchMachine::Data& data = transaction.data();
		if (!data.presenter)
		{
			return {false, "there is no search box to search from (is a project loaded?)"};
		}
		query = data.query;
		data.outcome = query.empty() ? std::string("searched for what the box holds")
									 : std::format("searched for '{}' as typed", query);
		data.matches.clear();
		transaction.move(Event::Search, source);
		presenter = data.presenter;
	}
	presenter.submitQuery();
	return {true, query.empty() ? std::string("searching") : std::format("searching for '{}'", query)};
}

bool SearchStateMachine::awaits(const std::string& query) const
{
	return m_machine.read([&](const SearchMachine::Data& data, State state) {
		return state == State::Completing && data.query == query;
	});
}

bool SearchStateMachine::completed(const std::string& query, const std::vector<SearchMatch>& matches)
{
	auto transaction = m_machine.transaction();
	SearchMachine::Data& data = transaction.data();
	if (transaction.state() != State::Completing || data.query != query)
	{
		return false;	// typed over or closed meanwhile
	}
	if (matches.empty())
	{
		// A full-text query has no completions: Enter (search) runs it.
		data.outcome = query.starts_with(SearchMatch::FULLTEXT_SEARCH_CHARACTER)
			? std::format("'{}' is a full-text query: search runs it", query)
			: std::format("nothing matches '{}'", query);
		transaction.move(Event::FoundNone);
		return true;	// the box closes an empty list
	}
	data.matches = matches;
	transaction.move(Event::Completed);
	return true;
}

MachineFireResult SearchStateMachine::fire(const std::string& event, const std::string& args, EventSource source)
{
	const std::optional<Event> control = m_machine.controlNamed(event);
	if (!control)
	{
		return m_machine.unknownEvent(event);
	}
	switch (*control)
	{
	case Event::Type:
	{
		const auto parsed = parseArgs<search_machine_json::TypeArgs>(event, args, R"({"query": "..."})");
		if (!parsed)
		{
			return {false, parsed.error()};
		}
		// What the box's filters search for, as a person typing there gets.
		const NodeTypeSet nodeTypes = m_machine.read(
			[](const SearchMachine::Data& data, State) { return data.boxNodeTypes; });
		return type(parsed->query, nodeTypes, source);
	}
	case Event::Select:
	{
		const auto parsed = parseArgs<search_machine_json::SelectArgs>(
			event, args, R"({"index": n} or {"name": "..."})");
		if (!parsed)
		{
			return {false, parsed.error()};
		}
		if (parsed->index)
		{
			return select(*parsed->index, source);
		}
		if (!parsed->name)
		{
			return {false, R"(select needs {"index": n} or {"name": "..."})"};
		}
		const auto index = m_machine.read(
			[&](const SearchMachine::Data& data, State) -> std::expected<std::size_t, std::string> {
				const auto it = std::ranges::find(data.matches, *parsed->name, &SearchMatch::getFullName);
				if (it == data.matches.end())
				{
					return std::unexpected(std::format("'{}' is not suggested", *parsed->name));
				}
				return static_cast<std::size_t>(it - data.matches.begin());
			});
		if (!index)
		{
			return {false, index.error()};
		}
		return select(*index, source);
	}
	case Event::Dismiss:
		return dismiss(source);
	case Event::Search:
		return search(source);
	default:
		return m_machine.unknownEvent(event);
	}
}

std::string SearchMachine::context(State state, const Data& data)
{
	search_machine_json::SearchContext context;
	if (!data.tokens.empty())
	{
		std::vector<std::string> tokens;
		tokens.reserve(data.tokens.size());
		for (const SearchMatch& token: data.tokens)
		{
			tokens.push_back(token.getFullName());
		}
		context.tokens = std::move(tokens);
	}
	if (state != State::Idle)
	{
		context.query = data.query;
	}
	if (state == State::Suggesting)
	{
		std::vector<search_machine_json::Suggestion> suggestions;
		for (const SearchMatch& match: data.matches | std::views::take(listedSuggestions))
		{
			suggestions.push_back({match.getFullName(), match.typeName, match.getSearchTypeName()});
		}
		context.suggestions = std::move(suggestions);
		if (data.matches.size() > listedSuggestions)
		{
			context.more = data.matches.size() - listedSuggestions;
		}
	}
	if (state == State::Idle && !data.outcome.empty())
	{
		context.outcome = data.outcome;
	}
	return glz::write_json(context).value_or("{}");
}
