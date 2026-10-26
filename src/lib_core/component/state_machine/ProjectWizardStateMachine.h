#ifndef PROJECT_WIZARD_STATE_MACHINE_H
#define PROJECT_WIZARD_STATE_MACHINE_H

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ConcurrentStateMachine.h"
#include "Presenter.h"
#include "SourceGroupFields.h"

// What an agent asks the project wizard to do; the wizard does it on the GUI thread
// and reports how it went (ProjectWizardStateMachine::applied / failed / ...).
struct ProjectWizardRequest
{
	enum class Kind
	{
		New,	// open the wizard on a new project
		Edit,	// open it on the project loaded
		Set,	// the project's and the shown source group's fields
		Show,	// a page: the general one, or a source group's
		AddSourceGroup,	// of `type`, or open the type selection without one
		SelectType,	// in the type selection
		RemoveSourceGroup,
		DuplicateSourceGroup,	// a copy right after it, shown
		Create,	// create or save the project, and load it
		Cancel	// close the wizard, or the type selection
	};

	Kind kind = Kind::New;
	std::optional<std::string> projectName;
	std::optional<std::string> projectLocation;
	std::map<std::string, SourceGroupField> fields;
	std::optional<std::size_t> sourceGroup;	// Show, Remove/DuplicateSourceGroup (default: the shown one)
	SourceGroupType type = SourceGroupType::UNKNOWN;
};

// The wizard as it is: what its pages show, as of its last step (a person's typing on
// a page shows once they move on: another page, create).
struct ProjectWizardView
{
	struct SourceGroup
	{
		std::string name;
		std::string type;	// sourceGroupTypeId
		bool enabled = true;
	};

	bool editing = false;	// the project loaded, not a new one
	std::string projectName;
	std::string projectLocation;
	std::vector<SourceGroup> sourceGroups;
	std::optional<std::size_t> sourceGroup;	// the page shown: a source group's, or the general one
	std::map<std::string, SourceGroupField> fields;	// the shown source group's
	std::vector<std::string> types;	// what can be added (the type selection)
};

// Shows the wizard's states for an agent: opens the wizard, and has it carry out what
// an agent asked. What a person does there the wizard reports itself.
template <typename T>
concept PresentsProjectWizard = requires(T& presenter, const ProjectWizardRequest& request) {
	presenter.applyToProjectWizard(request);
};

struct ProjectWizardPresenterOperations
{
	void (*apply)(void* presenter, const ProjectWizardRequest& request);
};

// Any PresentsProjectWizard, by reference (Presenter.h).
class ProjectWizardPresenter: public Presenter<ProjectWizardPresenterOperations>
{
public:
	using Presenter::Presenter;

	template <PresentsProjectWizard T>
		requires(!std::derived_from<T, Presenter>)	  // a handle is copied, not referred to
	ProjectWizardPresenter(T& presenter): Presenter(presenter, &operations<T>)
	{
	}

	void applyToProjectWizard(const ProjectWizardRequest& request) const
	{
		m_operations->apply(m_presenter, request);
	}

private:
	template <PresentsProjectWizard T>
	static constexpr ProjectWizardPresenterOperations operations {
		[](void* presenter, const ProjectWizardRequest& request) {
			static_cast<T*>(presenter)->applyToProjectWizard(request);
		}};
};

// The project wizard ("New Project" / "Edit Project"), from opening it until the
// project is created or the wizard closed:
//
//   closed --new/edit--> applying --opened--> editing --create--> applying --created--> closed
//   editing --set/show/add/remove/duplicate_source_group--> applying --applied/failed--> editing
//   editing --choosing--> choosing_type --select_type--> applying; choosing_type --selected/back--> editing
//   editing --cancel--> applying --canceled--> closed
//
// `applying` lasts while the wizard carries out an agent's request on the GUI thread;
// a check on the way may ask a question (QuestionStateMachine) first. A person's steps
// move the machine directly: opening the wizard, the type selection, creating,
// closing; the context follows their page changes.
struct ProjectWizardMachine
{
	enum class State
	{
		Closed,
		Editing,
		ChoosingType,
		Applying
	};
	enum class Event
	{
		New,
		Edit,
		Set,
		Show,
		AddSourceGroup,
		SelectType,
		RemoveSourceGroup,
		DuplicateSourceGroup,
		Create,
		Cancel,
		Opened,
		NotOpened,
		Applied,
		Failed,
		Choosing,
		Selected,
		Back,
		Created,
		Canceled
	};

	struct Data
	{
		std::optional<ProjectWizardView> view;	// while the wizard is open
		std::string request;	// what applying carries out
		std::string error;	// why the last request failed
		std::string outcome;	// how the wizard last closed
		ProjectWizardPresenter presenter;
	};

	static constexpr std::string_view name = "project_wizard";
	static constexpr State initial = State::Closed;
	static constexpr std::array states {
		named(State::Closed, "closed"),
		named(State::Editing, "editing"),
		named(State::ChoosingType, "choosing_type"),
		named(State::Applying, "applying")};
	static constexpr std::array events {
		control(Event::New, "new"),
		control(Event::Edit, "edit"),
		control(Event::Set, "set"),
		control(Event::Show, "show"),
		control(Event::AddSourceGroup, "add_source_group"),
		control(Event::SelectType, "select_type"),
		control(Event::RemoveSourceGroup, "remove_source_group"),
		control(Event::DuplicateSourceGroup, "duplicate_source_group"),
		control(Event::Create, "create"),
		control(Event::Cancel, "cancel"),
		named(Event::Opened, "opened"),
		named(Event::NotOpened, "not_opened"),
		named(Event::Applied, "applied"),
		named(Event::Failed, "failed"),
		named(Event::Choosing, "choosing"),
		named(Event::Selected, "selected"),
		named(Event::Back, "back"),
		named(Event::Created, "created"),
		named(Event::Canceled, "canceled")};
	static constexpr auto transitions = std::to_array<StateMachine<State, Event>::Transition>({
		{State::Closed, Event::New, State::Applying},
		{State::Closed, Event::Edit, State::Applying},
		{State::Closed, Event::Opened, State::Editing},
		{State::Editing, Event::Set, State::Applying},
		{State::Editing, Event::Show, State::Applying},
		{State::Editing, Event::AddSourceGroup, State::Applying},
		{State::Editing, Event::RemoveSourceGroup, State::Applying},
		{State::Editing, Event::DuplicateSourceGroup, State::Applying},
		{State::Editing, Event::Create, State::Applying},
		{State::Editing, Event::Cancel, State::Applying},
		{State::Editing, Event::Choosing, State::ChoosingType},
		{State::Editing, Event::Created, State::Closed},
		{State::Editing, Event::Canceled, State::Closed},
		{State::Editing, Event::Opened, State::Editing},
		{State::ChoosingType, Event::SelectType, State::Applying},
		{State::ChoosingType, Event::Cancel, State::Applying},
		{State::ChoosingType, Event::Selected, State::Editing},
		{State::ChoosingType, Event::Back, State::Editing},
		{State::ChoosingType, Event::Canceled, State::Closed},
		{State::Applying, Event::Opened, State::Editing},
		{State::Applying, Event::NotOpened, State::Closed},
		{State::Applying, Event::Applied, State::Editing},
		{State::Applying, Event::Failed, State::Editing},
		{State::Applying, Event::Choosing, State::ChoosingType},
		{State::Applying, Event::Created, State::Closed},
		{State::Applying, Event::Canceled, State::Closed},
	});

	static std::string context(State state, const Data& data);
};

class ProjectWizardStateMachine
{
public:
	using State = ProjectWizardMachine::State;
	using Request = ProjectWizardRequest;

	static std::shared_ptr<ProjectWizardStateMachine> instance();

	// The main window: opens the wizard and passes it what an agent asks.
	void attach(ProjectWizardPresenter presenter);
	void detach(ProjectWizardPresenter presenter);

	// The controls: `request` to the wizard, from `source`.
	MachineFireResult request(Request request, EventSource source);

	// The wizard's side: what it did, and how it is now.
	void opened(ProjectWizardView view);
	void notOpened(const std::string& why);
	void applied(ProjectWizardView view);
	void failed(ProjectWizardView view, const std::string& why);
	void choosing(ProjectWizardView view);
	void selected(ProjectWizardView view);
	void back(ProjectWizardView view);
	void created(const std::string& project);
	void canceled();
	// A person changed the page (no move).
	void changed(ProjectWizardView view);

	MachineSnapshot snapshot() const
	{
		return m_machine.snapshot();
	}
	MachineFireResult fire(const std::string& event, const std::string& args, EventSource source);

private:
	using Machine = ConcurrentStateMachine<ProjectWizardMachine>;
	using Event = ProjectWizardMachine::Event;

	// Moves along `event` with `view` as the wizard is now; true when it did.
	bool report(Event event, std::optional<ProjectWizardView> view, const std::string& error = {});

	Machine m_machine;
};

#endif	  // PROJECT_WIZARD_STATE_MACHINE_H
