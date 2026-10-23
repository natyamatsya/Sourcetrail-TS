#ifndef BOOKMARK_STATE_MACHINE_H
#define BOOKMARK_STATE_MACHINE_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ConcurrentStateMachine.h"
#include "Presenter.h"

// What the bookmark editor edits.
struct BookmarkFields
{
	std::string name;
	std::string comment;
	std::string category;
};

// Shows the bookmark editor's state for someone else (an agent):
// presentBookmarkFields(fields) fills its fields, dismissBookmarkEditor() closes it.
// What a person does there the editor reports itself.
template <typename T>
concept PresentsBookmarkEditor = requires(T& presenter, const BookmarkFields& fields) {
	presenter.presentBookmarkFields(fields);
	presenter.dismissBookmarkEditor();
};

struct BookmarkPresenterOperations
{
	void (*presentFields)(void* presenter, const BookmarkFields& fields);
	void (*dismiss)(void* presenter);
};

// Any PresentsBookmarkEditor, by reference (Presenter.h).
class BookmarkPresenter: public Presenter<BookmarkPresenterOperations>
{
public:
	using Presenter::Presenter;

	template <PresentsBookmarkEditor T>
		requires(!std::derived_from<T, Presenter>)	  // a handle is copied, not referred to
	BookmarkPresenter(T& presenter): Presenter(presenter, &operations<T>)
	{
	}

	void presentBookmarkFields(const BookmarkFields& fields) const
	{
		m_operations->presentFields(m_presenter, fields);
	}
	void dismissBookmarkEditor() const
	{
		m_operations->dismiss(m_presenter);
	}

private:
	template <PresentsBookmarkEditor T>
	static constexpr BookmarkPresenterOperations operations {
		[](void* presenter, const BookmarkFields& fields) { static_cast<T*>(presenter)->presentBookmarkFields(fields); },
		[](void* presenter) { static_cast<T*>(presenter)->dismissBookmarkEditor(); }};
};

// The bookmark editor ("Create Bookmark" / "Edit Bookmark"), from the request until it
// is saved or canceled:
//
//   idle --create/edit--> opening --opened--> editing --save/cancel--> idle
//   opening --nothing/timed_out--> idle; idle --opened--> editing (a person opened it)
//
// `create` bookmarks what is active (or a node), as the bookmark button does: when it
// is bookmarked already, the editor edits that bookmark. `edit` opens an existing
// bookmark, as the browser's edit button does. A person's typing in the editor
// changes its fields here as `set` does, without a move; its buttons send save and
// cancel. Deleting a bookmark or a category asks a question (QuestionStateMachine).
struct BookmarkMachine
{
	enum class State
	{
		Idle,
		Opening,
		Editing
	};
	enum class Event
	{
		Create,
		Edit,
		Opened,
		Nothing,
		TimedOut,
		Set,
		Save,
		Cancel
	};

	// What the editor is opened on.
	struct Editor
	{
		std::uint64_t id = 0;	// which editor window (opened gives it)
		std::optional<std::uint64_t> bookmarkId;	// editing an existing bookmark
		std::uint64_t nodeId = 0;	// creating for a node, not what is active
		BookmarkFields fields;
		std::vector<std::string> categories;	// to choose from (or name a new one)
	};

	struct Data
	{
		Editor editor;
		std::function<void(const BookmarkFields&)> commit;	// the editor's: creates or updates it
		std::string outcome;	// how the last request ended
		std::chrono::milliseconds openingLimit {5000};
		std::uint64_t nextEditor = 1;
		BookmarkPresenter presenter;
	};

	static constexpr std::string_view name = "bookmark";
	static constexpr State initial = State::Idle;
	static constexpr std::array states {
		named(State::Idle, "idle"), named(State::Opening, "opening"), named(State::Editing, "editing")};
	static constexpr std::array events {
		control(Event::Create, "create"),
		control(Event::Edit, "edit"),
		named(Event::Opened, "opened"),
		named(Event::Nothing, "nothing"),
		named(Event::TimedOut, "timed_out"),
		control(Event::Set, "set"),
		control(Event::Save, "save"),
		control(Event::Cancel, "cancel")};
	static constexpr auto transitions = std::to_array<StateMachine<State, Event>::Transition>({
		{State::Idle, Event::Create, State::Opening},
		{State::Idle, Event::Edit, State::Opening},
		{State::Idle, Event::Opened, State::Editing},
		{State::Opening, Event::Opened, State::Editing},
		{State::Opening, Event::Nothing, State::Idle},
		{State::Opening, Event::TimedOut, State::Idle},
		{State::Editing, Event::Opened, State::Editing},
		{State::Editing, Event::Set, State::Editing},
		{State::Editing, Event::Save, State::Idle},
		{State::Editing, Event::Cancel, State::Idle},
	});

	static std::string context(State state, const Data& data);
};

class BookmarkStateMachine
{
public:
	using State = BookmarkMachine::State;
	using Editor = BookmarkMachine::Editor;

	static std::shared_ptr<BookmarkStateMachine> instance();

	// The bookmark view: shows what an agent does in the editor (one at a time).
	void attach(BookmarkPresenter presenter);
	void detach(BookmarkPresenter presenter);

	// The controls.

	// Opens the editor on what is active, or on node `nodeId` (MessageBookmarkCreate).
	MachineFireResult create(std::uint64_t nodeId, EventSource source);
	// Opens the editor on bookmark `bookmarkId` (MessageBookmarkEdit).
	MachineFireResult edit(std::uint64_t bookmarkId, EventSource source);
	// Changes the editor's fields: those given.
	MachineFireResult set(
		std::optional<std::string> name,
		std::optional<std::string> comment,
		std::optional<std::string> category,
		EventSource source);
	// Creates or updates the bookmark with the editor's fields (`fields`: a person's,
	// from the editor), and closes it. `editor`: only if that editor is the one open.
	MachineFireResult save(
		std::optional<BookmarkFields> fields, EventSource source, std::optional<std::uint64_t> editor = std::nullopt);
	MachineFireResult cancel(EventSource source, std::optional<std::uint64_t> editor = std::nullopt);

	// The editor's side: it opened (`commit` saves its fields): its id, for what it
	// reports; a person changed its fields.
	std::uint64_t opened(Editor editor, std::function<void(const BookmarkFields&)> commit);
	void edited(std::uint64_t editor, const BookmarkFields& fields);
	// The bookmark controller's side: a request opens nothing (`why`).
	void nothing(const std::string& why);

	// How long `opening` waits for the editor (nobody to open it: no project).
	void setOpeningLimit(std::chrono::milliseconds limit);

	MachineSnapshot snapshot() const
	{
		return m_machine.snapshot();
	}
	MachineFireResult fire(const std::string& event, const std::string& args, EventSource source);

private:
	using Machine = ConcurrentStateMachine<BookmarkMachine>;
	using Event = BookmarkMachine::Event;

	// Moves to opening, for `event`, until the editor opens or `limit` passes.
	MachineFireResult request(Event event, EventSource source);

	Machine m_machine;
};

#endif	  // BOOKMARK_STATE_MACHINE_H
