#include "BookmarkStateMachine.h"

#include <format>

#include "MachineArgs.h"
#include "MessageBookmarkCreate.h"
#include "MessageBookmarkDelete.h"
#include "MessageBookmarkEdit.h"
#include "logging.h"
#include "utilityString.h"

// glaze reflects these for JSON; MSVC cannot reflect types in an anonymous namespace.
namespace bookmark_machine_json
{
struct EditorContext
{
	std::string mode;	// create | edit
	std::optional<std::uint64_t> bookmark_id;
	std::optional<std::uint64_t> node_id;
	std::string name;
	std::string comment;
	std::string category;
	std::vector<std::string> categories;
};

struct BookmarkContext
{
	std::optional<EditorContext> editor;
	std::optional<std::string> outcome;
};

struct CreateArgs
{
	std::uint64_t node_id = 0;	// default: what is active
};

struct EditArgs
{
	std::uint64_t id = 0;
};

struct SetArgs
{
	std::optional<std::string> name;
	std::optional<std::string> comment;
	std::optional<std::string> category;
};
}	 // namespace bookmark_machine_json

std::shared_ptr<BookmarkStateMachine> BookmarkStateMachine::instance()
{
	static const std::shared_ptr<BookmarkStateMachine> machine = std::make_shared<BookmarkStateMachine>();
	return machine;
}

void BookmarkStateMachine::attach(BookmarkPresenter presenter)
{
	auto transaction = m_machine.transaction();
	transaction.data().presenter = presenter;
}

void BookmarkStateMachine::detach(BookmarkPresenter presenter)
{
	auto transaction = m_machine.transaction();
	if (transaction.data().presenter == presenter)
	{
		transaction.data().presenter = {};
	}
}

MachineFireResult BookmarkStateMachine::request(Event event, EventSource source)
{
	auto transaction = m_machine.transaction();
	if (!transaction.move(event, source))
	{
		return transaction.refuse(event);
	}
	BookmarkMachine::Data& data = transaction.data();
	data.outcome.clear();
	transaction.timeout(data.openingLimit, [](Machine::Transaction& expired) {
		expired.data().outcome = "the editor did not open (is a project loaded?)";
		expired.move(Event::TimedOut);
	});
	return {true, "opening the editor"};
}

MachineFireResult BookmarkStateMachine::create(std::uint64_t nodeId, EventSource source)
{
	MachineFireResult result = request(Event::Create, source);
	if (result.ok)
	{
		MessageBookmarkCreate(static_cast<Id>(nodeId)).dispatch();
	}
	return result;
}

MachineFireResult BookmarkStateMachine::edit(std::uint64_t bookmarkId, EventSource source)
{
	MachineFireResult result = request(Event::Edit, source);
	if (result.ok)
	{
		MessageBookmarkEdit(bookmarkId).dispatch();
	}
	return result;
}

MachineFireResult BookmarkStateMachine::set(
	std::optional<std::string> name,
	std::optional<std::string> comment,
	std::optional<std::string> category,
	EventSource source)
{
	BookmarkFields fields;
	BookmarkPresenter presenter;
	{
		auto transaction = m_machine.transaction();
		if (!transaction.accepts(Event::Set))
		{
			return transaction.refuse(Event::Set);
		}
		BookmarkFields& current = transaction.data().editor.fields;
		if (name)
		{
			current.name = std::move(*name);
		}
		if (comment)
		{
			current.comment = std::move(*comment);
		}
		if (category)
		{
			current.category = std::move(*category);
		}
		fields = current;
		transaction.move(Event::Set, source);
		presenter = transaction.data().presenter;
	}
	if (presenter && source != EventSource::Ui)
	{
		presenter.presentBookmarkFields(fields);
	}
	return {true, "set"};
}

MachineFireResult BookmarkStateMachine::save(
	std::optional<BookmarkFields> fields, EventSource source, std::optional<std::uint64_t> editor)
{
	std::function<void(const BookmarkFields&)> commit;
	BookmarkFields saved;
	BookmarkPresenter presenter;
	bool created = false;
	{
		auto transaction = m_machine.transaction();
		if (!transaction.accepts(Event::Save))
		{
			return transaction.refuse(Event::Save);
		}
		BookmarkMachine::Data& data = transaction.data();
		if (editor && *editor != data.editor.id)
		{
			return {false, "another editor is open"};
		}
		if (fields)
		{
			data.editor.fields = std::move(*fields);
		}
		// A name or category of spaces is none ("   " was saved as a bookmark's name),
		// and spaces around them are not part of them.
		data.editor.fields.name = utility::trim(data.editor.fields.name);
		data.editor.fields.category = utility::trim(data.editor.fields.category);
		if (data.editor.fields.name.empty())
		{
			return {false, "a bookmark needs a name (set { name })"};
		}
		saved = data.editor.fields;
		created = !data.editor.bookmarkId;
		commit = std::move(data.commit);
		data.outcome = std::format("{} '{}'", created ? "created" : "updated", saved.name);
		transaction.move(Event::Save, source);
		presenter = data.presenter;
	}
	if (commit)
	{
		commit(saved);
	}
	if (presenter && source != EventSource::Ui)
	{
		presenter.dismissBookmarkEditor();
	}
	return {true, std::format("{} '{}'", created ? "creating" : "updating", saved.name)};
}

MachineFireResult BookmarkStateMachine::cancel(EventSource source, std::optional<std::uint64_t> editor)
{
	BookmarkPresenter presenter;
	{
		auto transaction = m_machine.transaction();
		if (!transaction.accepts(Event::Cancel))
		{
			return transaction.refuse(Event::Cancel);
		}
		BookmarkMachine::Data& data = transaction.data();
		if (editor && *editor != data.editor.id)
		{
			return {false, "another editor is open"};
		}
		data.commit = nullptr;
		data.outcome = "canceled";
		transaction.move(Event::Cancel, source);
		presenter = data.presenter;
	}
	if (presenter && source != EventSource::Ui)
	{
		presenter.dismissBookmarkEditor();
	}
	return {true, "canceled"};
}

std::uint64_t BookmarkStateMachine::opened(Editor editor, std::function<void(const BookmarkFields&)> commit)
{
	auto transaction = m_machine.transaction();
	BookmarkMachine::Data& data = transaction.data();
	data.editor = std::move(editor);
	data.editor.id = data.nextEditor++;
	data.commit = std::move(commit);
	data.outcome.clear();
	if (!transaction.move(Event::Opened))
	{
		transaction.defer([state = m_machine.stateName(transaction.state())]() {
			LOG_WARNING(std::format("bookmark state machine: the editor opened in state {}", state));
		});
	}
	return data.editor.id;
}

void BookmarkStateMachine::edited(std::uint64_t editor, const BookmarkFields& fields)
{
	auto transaction = m_machine.transaction();
	if (transaction.state() == State::Editing && transaction.data().editor.id == editor)
	{
		transaction.data().editor.fields = fields;
	}
}

void BookmarkStateMachine::nothing(const std::string& why)
{
	auto transaction = m_machine.transaction();
	if (transaction.state() == State::Opening)
	{
		transaction.data().outcome = why;
		transaction.move(Event::Nothing);
	}
}

MachineFireResult BookmarkStateMachine::remove(std::uint64_t bookmarkId, EventSource source)
{
	{
		auto transaction = m_machine.transaction();
		if (!transaction.move(Event::Delete, source))
		{
			return transaction.refuse(Event::Delete);
		}
		transaction.data().outcome = std::format("asked whether to delete bookmark {}", bookmarkId);
	}
	MessageBookmarkDelete(bookmarkId).dispatch();
	return {true, std::format("asking whether to delete bookmark {} (see question)", bookmarkId)};
}

void BookmarkStateMachine::report(const std::string& outcome)
{
	auto transaction = m_machine.transaction();
	if (transaction.state() == State::Idle)
	{
		transaction.data().outcome = outcome;
	}
}

void BookmarkStateMachine::setOpeningLimit(std::chrono::milliseconds limit)
{
	auto transaction = m_machine.transaction();
	transaction.data().openingLimit = limit;
}

MachineFireResult BookmarkStateMachine::fire(const std::string& event, const std::string& args, EventSource source)
{
	const std::optional<Event> control = m_machine.controlNamed(event);
	if (!control)
	{
		return m_machine.unknownEvent(event);
	}
	switch (*control)
	{
	case Event::Create:
	{
		const auto parsed = parseArgs<bookmark_machine_json::CreateArgs>(event, args, R"({"node_id"?: n})");
		return parsed ? create(parsed->node_id, source) : MachineFireResult {false, parsed.error()};
	}
	case Event::Edit:
	{
		const auto parsed = parseArgs<bookmark_machine_json::EditArgs>(event, args, R"({"id": n})");
		if (!parsed)
		{
			return {false, parsed.error()};
		}
		if (parsed->id == 0)
		{
			return {false, R"(edit needs the bookmark's {"id": n} (ui_state.bookmarks))"};
		}
		return edit(parsed->id, source);
	}
	case Event::Set:
	{
		const auto parsed = parseArgs<bookmark_machine_json::SetArgs>(
			event, args, R"({"name"?: "...", "comment"?: "...", "category"?: "..."})");
		if (!parsed)
		{
			return {false, parsed.error()};
		}
		return set(parsed->name, parsed->comment, parsed->category, source);
	}
	case Event::Delete:
	{
		const auto parsed = parseArgs<bookmark_machine_json::EditArgs>(event, args, R"({"id": n})");
		if (!parsed)
		{
			return {false, parsed.error()};
		}
		if (parsed->id == 0)
		{
			return {false, R"(delete needs the bookmark's {"id": n} (ui_state.bookmarks))"};
		}
		return remove(parsed->id, source);
	}
	case Event::Save:
		return save(std::nullopt, source);
	case Event::Cancel:
		return cancel(source);
	default:
		return m_machine.unknownEvent(event);
	}
}

std::string BookmarkMachine::context(State state, const Data& data)
{
	bookmark_machine_json::BookmarkContext context;
	if (state == State::Editing)
	{
		const Editor& editor = data.editor;
		context.editor = bookmark_machine_json::EditorContext {
			editor.bookmarkId ? "edit" : "create",
			editor.bookmarkId,
			editor.nodeId != 0 ? std::optional(editor.nodeId) : std::nullopt,
			editor.fields.name,
			editor.fields.comment,
			editor.fields.category,
			editor.categories};
	}
	if (state == State::Idle && !data.outcome.empty())
	{
		context.outcome = data.outcome;
	}
	return glz::write_json(context).value_or("{}");
}
