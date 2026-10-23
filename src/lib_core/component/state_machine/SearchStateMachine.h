#ifndef SEARCH_STATE_MACHINE_H
#define SEARCH_STATE_MACHINE_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ConcurrentStateMachine.h"
#include "NodeTypeSet.h"
#include "Presenter.h"
#include "SearchMatch.h"

// Shows what the search machine does to the search box for someone else (an agent):
// presentQuery(query) shows a query as typed, dismissSuggestions() closes the list.
// What a person does there the box reports itself.
template <typename T>
concept PresentsSearch = requires(T& presenter, const std::string& query) {
	presenter.presentQuery(query);
	presenter.dismissSuggestions();
};

struct SearchPresenterOperations
{
	void (*presentQuery)(void* presenter, const std::string& query);
	void (*dismissSuggestions)(void* presenter);
};

// Any PresentsSearch, by reference (Presenter.h).
class SearchPresenter: public Presenter<SearchPresenterOperations>
{
public:
	using Presenter::Presenter;

	template <PresentsSearch T>
		requires(!std::derived_from<T, Presenter>)	  // a handle is copied, not referred to
	SearchPresenter(T& presenter): Presenter(presenter, &operations<T>)
	{
	}

	void presentQuery(const std::string& query) const
	{
		m_operations->presentQuery(m_presenter, query);
	}
	void dismissSuggestions() const
	{
		m_operations->dismissSuggestions(m_presenter);
	}

private:
	template <PresentsSearch T>
	static constexpr SearchPresenterOperations operations {
		[](void* presenter, const std::string& query) { static_cast<T*>(presenter)->presentQuery(query); },
		[](void* presenter) { static_cast<T*>(presenter)->dismissSuggestions(); }};
};

// The search box's autocompletion, from a query typed until the list it brings up is
// chosen from or closed:
//
//   idle --type--> completing --completed--> suggesting --select/dismiss--> idle
//   completing --found_none/dismiss--> idle; completing/suggesting --type--> completing
//
// A person typing sends `type` (the search box does), and closing the list or
// searching sends `dismiss`; an agent sends the same, and `select` picks one of the
// suggestions: its search runs as a click on it in the list would. Completions for a
// query typed over since are not shown.
struct SearchMachine
{
	enum class State
	{
		Idle,
		Completing,
		Suggesting
	};
	enum class Event
	{
		Type,
		Completed,
		FoundNone,
		Select,
		Dismiss
	};

	struct Data
	{
		std::string query;	// typed last
		NodeTypeSet nodeTypes;	// what the query searches for (the box's filters)
		std::vector<SearchMatch> matches;	// suggesting
		std::string outcome;	// how the last query ended
		SearchPresenter presenter;
	};

	static constexpr std::string_view name = "search";
	static constexpr State initial = State::Idle;
	static constexpr std::array states {
		named(State::Idle, "idle"), named(State::Completing, "completing"), named(State::Suggesting, "suggesting")};
	static constexpr std::array events {
		control(Event::Type, "type"),
		named(Event::Completed, "completed"),
		named(Event::FoundNone, "found_none"),
		control(Event::Select, "select"),
		control(Event::Dismiss, "dismiss")};
	static constexpr auto transitions = std::to_array<StateMachine<State, Event>::Transition>({
		{State::Idle, Event::Type, State::Completing},
		{State::Completing, Event::Type, State::Completing},
		{State::Suggesting, Event::Type, State::Completing},
		{State::Completing, Event::Completed, State::Suggesting},
		{State::Completing, Event::FoundNone, State::Idle},
		{State::Completing, Event::Dismiss, State::Idle},
		{State::Suggesting, Event::Select, State::Idle},
		{State::Suggesting, Event::Dismiss, State::Idle},
	});

	static std::string context(State state, const Data& data);
};

class SearchStateMachine
{
public:
	using State = SearchMachine::State;

	static std::shared_ptr<SearchStateMachine> instance();

	// The search box: shows what an agent does there (one at a time).
	void attach(SearchPresenter presenter);
	void detach(SearchPresenter presenter);

	// The controls.

	// Types `query` (searching for `nodeTypes`) and asks for its completions
	// (MessageSearchAutocomplete).
	MachineFireResult type(const std::string& query, NodeTypeSet nodeTypes, EventSource source);
	// Searches for suggestion `index` (MessageSearch).
	MachineFireResult select(std::size_t index, EventSource source);
	// Closes the suggestions, or stops waiting for them.
	MachineFireResult dismiss(EventSource source);

	// The search controller's side: whether completions for `query` are still waited
	// for, and they are here: true when they are to be shown.
	bool awaits(const std::string& query) const;
	bool completed(const std::string& query, const std::vector<SearchMatch>& matches);

	MachineSnapshot snapshot() const
	{
		return m_machine.snapshot();
	}
	MachineFireResult fire(const std::string& event, const std::string& args, EventSource source);

private:
	using Event = SearchMachine::Event;

	ConcurrentStateMachine<SearchMachine> m_machine;
};

#endif	  // SEARCH_STATE_MACHINE_H
