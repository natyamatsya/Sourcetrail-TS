#ifndef PREFERENCES_STATE_MACHINE_H
#define PREFERENCES_STATE_MACHINE_H

#include <cstdint>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ConcurrentStateMachine.h"
#include "Presenter.h"

// A preference as the Preferences dialog shows it and an agent sets it: a check box's
// true/false, a text field's text or a list's choice ("14", "dark", "system"), a path
// list's paths. Named after its row: font_size, color_scheme, header_search_paths, ...
using PreferenceField = std::variant<bool, std::string, std::vector<std::string>>;
using PreferenceFields = std::map<std::string, PreferenceField>;

// Why a preference could not be set to a value.
enum class PreferenceFieldError : std::uint8_t
{
	NotAPreference,
	Disabled,
	NotTrueOrFalse,
	NotAText,
	NotAList,
	NoSuchItem,
	NoSuchFont
};

constexpr std::string_view to_std_sv(PreferenceFieldError error) noexcept
{
	using enum PreferenceFieldError;
	switch (error)
	{
	case NotAPreference: return "is not a preference";
	case Disabled: return "cannot be changed now (it is disabled)";
	case NotTrueOrFalse: return "is true or false";
	case NotAText: return "is a text";
	case NotAList: return "is a list of paths";
	case NoSuchItem: return "has no such item";
	case NoSuchFont: return "is a monospaced font of this system, and that is none";
	}
	return "cannot be set";
}

// A preference's error, and what it has to choose from when that is the trouble.
struct PreferenceFieldFailure
{
	PreferenceFieldError error = PreferenceFieldError::NotAPreference;
	std::string detail;

	// For the agent: "'font_size' has no such item (it has "4", "5", ...)".
	[[nodiscard]] std::string message(std::string_view name) const
	{
		return detail.empty() ? std::format("'{}' {}", name, to_std_sv(error))
							  : std::format("'{}' {} ({})", name, to_std_sv(error), detail);
	}
};

// What an agent asks the Preferences dialog to do; the dialog does it on the GUI thread
// and reports how it went (PreferencesStateMachine::applied / failed / saved ...).
struct PreferencesRequest
{
	enum class Kind
	{
		Open,	// open the dialog, as Edit > Preferences does
		Set,	// fill its fields, as a person does (a colour scheme previews)
		Save,	// its Save button: checks the fields, saves them and closes it
		Cancel	// its Cancel button
	};

	Kind kind = Kind::Open;
	PreferenceFields fields;	// Set
};

// Shows the Preferences dialog's states for an agent: opens it, and has it carry out
// what an agent asked. What a person does there the dialog reports itself.
template <typename T>
concept PresentsPreferences = requires(T& presenter, const PreferencesRequest& request) {
	presenter.applyToPreferences(request);
};

struct PreferencesPresenterOperations
{
	void (*apply)(void* presenter, const PreferencesRequest& request);
};

// Any PresentsPreferences, by reference (Presenter.h).
class PreferencesPresenter: public Presenter<PreferencesPresenterOperations>
{
public:
	using Presenter::Presenter;

	template <PresentsPreferences T>
		requires(!std::derived_from<T, Presenter>)	  // a handle is copied, not referred to
	PreferencesPresenter(T& presenter): Presenter(presenter, &operations<T>)
	{
	}

	void applyToPreferences(const PreferencesRequest& request) const
	{
		m_operations->apply(m_presenter, request);
	}

private:
	template <PresentsPreferences T>
	static constexpr PreferencesPresenterOperations operations {
		[](void* presenter, const PreferencesRequest& request) {
			static_cast<T*>(presenter)->applyToPreferences(request);
		}};
};

// The Preferences dialog (Edit > Preferences), from opening it until it is saved or
// closed:
//
//   closed --open--> applying --opened--> editing --set/save/cancel--> applying
//   applying --applied/failed--> editing; applying --saved/canceled--> closed
//
// `applying` lasts while the dialog carries out an agent's request on the GUI thread;
// Save's checks may ask a question (QuestionStateMachine) first, and a field it refuses
// leaves the dialog open: back at editing with context.error. A person's steps move the
// machine directly: opening the dialog, saving, closing it.
struct PreferencesMachine
{
	enum class State
	{
		Closed,
		Editing,
		Applying
	};
	enum class Event
	{
		Open,
		Set,
		Save,
		Cancel,
		Opened,
		Applied,
		Failed,
		Saved,
		Canceled
	};

	struct Data
	{
		PreferenceFields fields;	// while the dialog is open, as of its last report
		std::string request;	// what applying carries out
		std::string error;	// why the last request failed
		std::string outcome;	// how the dialog last closed
		PreferencesPresenter presenter;
	};

	static constexpr std::string_view name = "preferences";
	static constexpr State initial = State::Closed;
	static constexpr std::array states {
		named(State::Closed, "closed"), named(State::Editing, "editing"), named(State::Applying, "applying")};
	static constexpr std::array events {
		control(Event::Open, "open"),
		control(Event::Set, "set"),
		control(Event::Save, "save"),
		control(Event::Cancel, "cancel"),
		named(Event::Opened, "opened"),
		named(Event::Applied, "applied"),
		named(Event::Failed, "failed"),
		named(Event::Saved, "saved"),
		named(Event::Canceled, "canceled")};
	static constexpr auto transitions = std::to_array<StateMachine<State, Event>::Transition>({
		{State::Closed, Event::Open, State::Applying},
		{State::Closed, Event::Opened, State::Editing},
		{State::Editing, Event::Set, State::Applying},
		{State::Editing, Event::Save, State::Applying},
		{State::Editing, Event::Cancel, State::Applying},
		{State::Editing, Event::Saved, State::Closed},
		{State::Editing, Event::Canceled, State::Closed},
		{State::Applying, Event::Opened, State::Editing},
		{State::Applying, Event::Applied, State::Editing},
		{State::Applying, Event::Failed, State::Editing},
		{State::Applying, Event::Saved, State::Closed},
		{State::Applying, Event::Canceled, State::Closed},
	});

	static std::string context(State state, const Data& data);
};

class PreferencesStateMachine
{
public:
	using State = PreferencesMachine::State;
	using Request = PreferencesRequest;

	static std::shared_ptr<PreferencesStateMachine> instance();

	// The main window: opens the dialog and passes it what an agent asks.
	void attach(PreferencesPresenter presenter);
	void detach(PreferencesPresenter presenter);

	// The controls: `request` to the dialog, from `source`.
	MachineFireResult request(Request request, EventSource source);

	// The dialog's side: what it did, and its fields now.
	void opened(PreferenceFields fields);
	void applied(PreferenceFields fields);
	void failed(PreferenceFields fields, const std::string& why);
	void saved();
	void canceled();

	MachineSnapshot snapshot() const
	{
		return m_machine.snapshot();
	}
	MachineFireResult fire(const std::string& event, const std::string& args, EventSource source);

private:
	using Machine = ConcurrentStateMachine<PreferencesMachine>;
	using Event = PreferencesMachine::Event;

	// Moves along `event` with the dialog's `fields`; true when it did.
	bool report(Event event, PreferenceFields fields, const std::string& error = {});
	// Moves along `event` (saved, canceled) to closed, with `outcome`.
	void closed(Event event, const std::string& outcome);

	Machine m_machine;
};

#endif	  // PREFERENCES_STATE_MACHINE_H
