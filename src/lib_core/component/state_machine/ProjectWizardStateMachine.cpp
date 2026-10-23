#include "ProjectWizardStateMachine.h"

#include <format>

#include "MachineArgs.h"

// glaze reflects these for JSON; MSVC cannot reflect types in an anonymous namespace.
namespace project_wizard_machine_json
{
struct Project
{
	std::string name;
	std::string location;
};

struct SourceGroup
{
	std::string name;
	std::string type;
	bool enabled = true;
};

struct WizardContext
{
	std::optional<std::string> mode;	// new | edit
	std::optional<std::string> page;	// general | source_group
	std::optional<std::size_t> source_group;
	std::optional<Project> project;
	std::optional<std::vector<SourceGroup>> source_groups;
	std::optional<std::map<std::string, SourceGroupField>> fields;
	std::optional<std::vector<std::string>> types;
	std::optional<std::string> applying;
	std::optional<std::string> error;
	std::optional<std::string> outcome;
};

struct SetArgs
{
	std::optional<std::string> project_name;
	std::optional<std::string> project_location;
	std::map<std::string, SourceGroupField> fields;
};

struct SourceGroupArgs
{
	std::optional<std::size_t> source_group;
};

struct TypeArgs
{
	std::optional<std::string> type;
};
}	 // namespace project_wizard_machine_json

namespace
{
std::string typeIds()
{
	std::string ids;
	for (SourceGroupType type:
		 {SourceGroupType::CXX_CDB,
		  SourceGroupType::CXX_CMAKE_FILE_API,
		  SourceGroupType::CXX_EMPTY,
		  SourceGroupType::C_EMPTY,
		  SourceGroupType::RUST_EMPTY,
		  SourceGroupType::SWIFT_EMPTY,
		  SourceGroupType::ZIG_EMPTY,
		  SourceGroupType::CUSTOM_COMMAND})
	{
		ids += (ids.empty() ? "" : ", ") + std::string(sourceGroupTypeId(type));
	}
	return ids;
}

std::expected<SourceGroupType, std::string> typeNamed(const std::optional<std::string>& id, std::string_view event)
{
	if (!id)
	{
		return std::unexpected(std::format(R"({} needs {{"type": ...}} ({}))", event, typeIds()));
	}
	const SourceGroupType type = sourceGroupTypeForId(*id);
	if (type == SourceGroupType::UNKNOWN)
	{
		return std::unexpected(std::format("'{}' is not a source group type ({})", *id, typeIds()));
	}
	return type;
}
}	 // namespace

std::shared_ptr<ProjectWizardStateMachine> ProjectWizardStateMachine::instance()
{
	static const std::shared_ptr<ProjectWizardStateMachine> machine = std::make_shared<ProjectWizardStateMachine>();
	return machine;
}

void ProjectWizardStateMachine::attach(ProjectWizardPresenter presenter)
{
	auto transaction = m_machine.transaction();
	transaction.data().presenter = presenter;
}

void ProjectWizardStateMachine::detach(ProjectWizardPresenter presenter)
{
	auto transaction = m_machine.transaction();
	if (transaction.data().presenter == presenter)
	{
		transaction.data().presenter = {};
	}
}

MachineFireResult ProjectWizardStateMachine::request(Request request, EventSource source)
{
	using Kind = Request::Kind;
	static constexpr std::array events {
		std::pair {Kind::New, Event::New},
		std::pair {Kind::Edit, Event::Edit},
		std::pair {Kind::Set, Event::Set},
		std::pair {Kind::Show, Event::Show},
		std::pair {Kind::AddSourceGroup, Event::AddSourceGroup},
		std::pair {Kind::SelectType, Event::SelectType},
		std::pair {Kind::RemoveSourceGroup, Event::RemoveSourceGroup},
		std::pair {Kind::Create, Event::Create},
		std::pair {Kind::Cancel, Event::Cancel},
	};
	const Event event = std::ranges::find(events, request.kind, &std::pair<Kind, Event>::first)->second;

	ProjectWizardPresenter presenter;
	{
		auto transaction = m_machine.transaction();
		if (!transaction.accepts(event))
		{
			return transaction.refuse(event);
		}
		ProjectWizardMachine::Data& data = transaction.data();
		if (!data.presenter)
		{
			return {false, "there is no window to show the wizard in"};
		}

		// What the wizard can tell before it is asked.
		const std::optional<ProjectWizardView>& view = data.view;
		if (request.sourceGroup && view && *request.sourceGroup >= view->sourceGroups.size())
		{
			return {false, std::format("there is no source group {} ({} in the project)", *request.sourceGroup, view->sourceGroups.size())};
		}
		if (request.kind == Kind::RemoveSourceGroup && !request.sourceGroup && view && !view->sourceGroup)
		{
			return {false, R"(remove_source_group needs {"source_group": n}, or a source group shown)"};
		}
		if (!request.fields.empty() && view && !view->sourceGroup)
		{
			return {false, R"(the fields are a source group's: show { "source_group": n } first)"};
		}
		for (const auto& [name, value]: request.fields)
		{
			if (view && !view->fields.contains(name))
			{
				std::string names;
				for (const auto& [field, current]: view->fields)
				{
					names += (names.empty() ? "" : ", ") + field;
				}
				return {false, std::format("'{}' is not a field of this source group (it has {})", name, names)};
			}
		}

		data.request = std::string(m_machine.eventName(event));
		data.error.clear();
		data.outcome.clear();
		transaction.move(event, source);
		presenter = data.presenter;
	}
	presenter.applyToProjectWizard(request);
	return {true, std::format("the wizard is on it ({}): until editing, closed or choosing_type", m_machine.eventName(event))};
}

bool ProjectWizardStateMachine::report(Event event, std::optional<ProjectWizardView> view, const std::string& error)
{
	auto transaction = m_machine.transaction();
	if (!transaction.accepts(event))
	{
		return false;	// a person's step while an agent's request is carried out, or after
	}
	ProjectWizardMachine::Data& data = transaction.data();
	data.view = std::move(view);
	data.error = error;
	data.request.clear();
	transaction.move(event);
	return true;
}

void ProjectWizardStateMachine::opened(ProjectWizardView view)
{
	report(Event::Opened, std::move(view));
}

void ProjectWizardStateMachine::notOpened(const std::string& why)
{
	auto transaction = m_machine.transaction();
	if (transaction.accepts(Event::NotOpened))
	{
		transaction.data().outcome = why;
		transaction.data().request.clear();
		transaction.move(Event::NotOpened);
	}
}

void ProjectWizardStateMachine::applied(ProjectWizardView view)
{
	report(Event::Applied, std::move(view));
}

void ProjectWizardStateMachine::failed(ProjectWizardView view, const std::string& why)
{
	report(Event::Failed, std::move(view), why);
}

void ProjectWizardStateMachine::choosing(ProjectWizardView view)
{
	report(Event::Choosing, std::move(view));
}

void ProjectWizardStateMachine::selected(ProjectWizardView view)
{
	report(Event::Selected, std::move(view));
}

void ProjectWizardStateMachine::back(ProjectWizardView view)
{
	report(Event::Back, std::move(view));
}

void ProjectWizardStateMachine::created(const std::string& project)
{
	auto transaction = m_machine.transaction();
	if (transaction.accepts(Event::Created))
	{
		ProjectWizardMachine::Data& data = transaction.data();
		data.outcome = std::format("{} {} (it loads: see project)", data.view && data.view->editing ? "saved" : "created", project);
		data.view.reset();
		data.request.clear();
		transaction.move(Event::Created);
	}
}

void ProjectWizardStateMachine::canceled()
{
	auto transaction = m_machine.transaction();
	if (transaction.accepts(Event::Canceled))
	{
		ProjectWizardMachine::Data& data = transaction.data();
		data.outcome = "canceled";
		data.view.reset();
		data.request.clear();
		transaction.move(Event::Canceled);
	}
}

void ProjectWizardStateMachine::changed(ProjectWizardView view)
{
	auto transaction = m_machine.transaction();
	if (transaction.state() != State::Closed)
	{
		transaction.data().view = std::move(view);
	}
}

MachineFireResult ProjectWizardStateMachine::fire(const std::string& event, const std::string& args, EventSource source)
{
	namespace json = project_wizard_machine_json;
	using Kind = Request::Kind;

	const std::optional<Event> control = m_machine.controlNamed(event);
	if (!control)
	{
		return m_machine.unknownEvent(event);
	}
	Request request;
	switch (*control)
	{
	case Event::New:
	case Event::Edit:
	case Event::Create:
	case Event::Cancel:
	{
		if (const auto parsed = parseArgs<machine_args::None>(event, args); !parsed)
		{
			return {false, parsed.error()};
		}
		request.kind = *control == Event::New ? Kind::New
			: *control == Event::Edit		  ? Kind::Edit
			: *control == Event::Create		  ? Kind::Create
											  : Kind::Cancel;
		break;
	}
	case Event::Set:
	{
		const auto parsed = parseArgs<json::SetArgs>(
			event, args, R"({"project_name"?: "...", "project_location"?: "...", "fields"?: {"source_paths": [...], ...}})");
		if (!parsed)
		{
			return {false, parsed.error()};
		}
		if (!parsed->project_name && !parsed->project_location && parsed->fields.empty())
		{
			return {false, "set needs project_name, project_location or fields"};
		}
		request.kind = Kind::Set;
		request.projectName = parsed->project_name;
		request.projectLocation = parsed->project_location;
		request.fields = parsed->fields;
		break;
	}
	case Event::Show:
	case Event::RemoveSourceGroup:
	{
		const auto parsed = parseArgs<json::SourceGroupArgs>(event, args, R"({"source_group"?: n})");
		if (!parsed)
		{
			return {false, parsed.error()};
		}
		request.kind = *control == Event::Show ? Kind::Show : Kind::RemoveSourceGroup;
		request.sourceGroup = parsed->source_group;
		break;
	}
	case Event::AddSourceGroup:
	case Event::SelectType:
	{
		const auto parsed = parseArgs<json::TypeArgs>(event, args, R"({"type": "cdb" | "cmake" | ...})");
		if (!parsed)
		{
			return {false, parsed.error()};
		}
		request.kind = *control == Event::AddSourceGroup ? Kind::AddSourceGroup : Kind::SelectType;
		if (parsed->type || *control == Event::SelectType)
		{
			const auto type = typeNamed(parsed->type, event);
			if (!type)
			{
				return {false, type.error()};
			}
			request.type = *type;
		}
		break;
	}
	default:
		return m_machine.unknownEvent(event);
	}
	return this->request(std::move(request), source);
}

std::string ProjectWizardMachine::context(State state, const Data& data)
{
	namespace json = project_wizard_machine_json;
	json::WizardContext context;
	if (state != State::Closed && data.view)
	{
		const ProjectWizardView& view = *data.view;
		context.mode = view.editing ? "edit" : "new";
		context.page = view.sourceGroup ? "source_group" : "general";
		context.source_group = view.sourceGroup;
		context.project = json::Project {view.projectName, view.projectLocation};
		std::vector<json::SourceGroup> groups;
		for (const ProjectWizardView::SourceGroup& group: view.sourceGroups)
		{
			groups.push_back({group.name, group.type, group.enabled});
		}
		context.source_groups = std::move(groups);
		if (view.sourceGroup)
		{
			context.fields = view.fields;
		}
		if (state == State::ChoosingType)
		{
			context.types = view.types;
		}
	}
	if (state == State::Applying && !data.request.empty())
	{
		context.applying = data.request;
	}
	if (!data.error.empty())
	{
		context.error = data.error;
	}
	if (state == State::Closed && !data.outcome.empty())
	{
		context.outcome = data.outcome;
	}
	return glz::write_json(context).value_or("{}");
}
