#include "DockStateMachine.h"

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <utility>

#include "MachineArgs.h"

// glaze reflects these for JSON; MSVC cannot reflect types in an anonymous namespace.
namespace dock_machine_json
{
struct DockEntry
{
	std::string name;
	bool open{false};
	bool visible{false};
	bool floating{false};
	std::optional<std::string> auto_hidden;
	std::optional<bool> overlay;
	std::optional<std::string> location;
	std::optional<int> x;
	std::optional<int> y;
	std::optional<int> width;
	std::optional<int> height;
	std::optional<std::vector<std::string>> tabs;
	std::optional<bool> current_tab;
};

struct DocksContext
{
	std::vector<DockEntry> docks;
	std::optional<std::string> applying;
	std::optional<std::string> error;
	std::optional<std::string> outcome;
};

struct DockArgs
{
	std::string dock;
};

struct DockBesideArgs
{
	std::string dock;
	std::string to;
	std::string of;
};

struct TabArgs
{
	std::string dock;
	std::string into;
};

struct ResizeArgs
{
	std::string dock;
	std::optional<int> width;
	std::optional<int> height;
};
}	 // namespace dock_machine_json

namespace
{
constexpr std::array dockSides {DockSide::Left, DockSide::Right, DockSide::Top, DockSide::Bottom, DockSide::Center};

constexpr DocksMachine::Event eventFor(DockRequest::Kind kind)
{
	switch (kind)
	{
		using enum DockRequest::Kind;
	case Show: return DocksMachine::Event::Show;
	case Close: return DocksMachine::Event::Close;
	case Float: return DocksMachine::Event::Float;
	case Dock: return DocksMachine::Event::Dock;
	case Tab: return DocksMachine::Event::Tab;
	case SelectTab: return DocksMachine::Event::SelectTab;
	case AutoHide: return DocksMachine::Event::AutoHide;
	case Restore: return DocksMachine::Event::Restore;
	case Resize: return DocksMachine::Event::Resize;
	case Reset: return DocksMachine::Event::Reset;
	}
	std::unreachable();
}

const DockState* findDock(const DockLayout& layout, std::string_view name)
{
	const auto it{std::ranges::find(layout, name, &DockState::name)};
	return it == layout.end() ? nullptr : &*it;
}

// "there are Search, Graph, Code, Status"
std::string dockNames(const DockLayout& layout)
{
	std::string names;
	for (const DockState& dock: layout)
	{
		names += std::format("{}{}", names.empty() ? "there are " : ", ", dock.name);
	}
	return names;
}

std::unexpected<DockRequestFailure> refuse(DockRequestError error, std::string dock, std::string detail = {})
{
	return std::unexpected(DockRequestFailure{error, std::move(dock), std::move(detail)});
}

// The dock named `name` in `layout`, or why there is none.
std::expected<const DockState*, DockRequestFailure> dockNamed(const DockLayout& layout, const std::string& name)
{
	if (name.empty())
	{
		return refuse(DockRequestError::NoDock, {}, dockNames(layout));
	}
	if (const DockState* dock{findDock(layout, name)})
	{
		return dock;
	}
	return refuse(DockRequestError::UnknownDock, name, dockNames(layout));
}
}	 // namespace

std::optional<DockSide> dockSideNamed(std::string_view name)
{
	const auto it{std::ranges::find(dockSides, name, [](DockSide side) { return to_std_sv(side); })};
	return it == dockSides.end() ? std::nullopt : std::optional<DockSide>{*it};
}

DockSide dockSideInLayout(const DockRect& dock, int width, int height, int slack)
{
	const bool left{dock.x <= slack};
	const bool right{dock.x + dock.width >= width - slack};
	const bool top{dock.y <= slack};
	const bool bottom{dock.y + dock.height >= height - slack};
	if (left != right)
	{
		return left ? DockSide::Left : DockSide::Right;
	}
	if (left && right && top != bottom)
	{
		return top ? DockSide::Top : DockSide::Bottom;
	}
	return DockSide::Center;
}

std::expected<void, DockRequestFailure> checkDockRequest(const DockLayout& layout, const DockRequest& request)
{
	using Kind = DockRequest::Kind;
	using enum DockRequestError;

	if (layout.empty())
	{
		return refuse(NoLayout, {});
	}
	if (request.kind == Kind::Reset)
	{
		return {};
	}
	const auto found{dockNamed(layout, request.dock)};
	if (!found)
	{
		return std::unexpected(found.error());
	}
	const DockState& dock{**found};

	switch (request.kind)
	{
	case Kind::Show:
		if (dock.visible)
		{
			return refuse(ShownAlready, dock.name);
		}
		break;
	case Kind::Close:
		if (!dock.open)
		{
			return refuse(ClosedAlready, dock.name);
		}
		if (std::ranges::none_of(layout, [&](const DockState& other) { return other.open && other.name != dock.name; }))
		{
			return refuse(LastDock, dock.name);
		}
		break;
	case Kind::Float:
		if (!dock.open)
		{
			return refuse(Closed, dock.name);
		}
		if (dock.floating)
		{
			return refuse(FloatingAlready, dock.name);
		}
		break;
	case Kind::Dock:
	{
		if (!request.to || *request.to == DockSide::Center)
		{
			return refuse(NoSide, dock.name, request.to ? "to tab it into a dock, use tab" : "");
		}
		if (request.other.empty())
		{
			break;	  // an edge of the whole layout
		}
		const auto other{dockNamed(layout, request.other)};
		if (!other)
		{
			return std::unexpected(other.error());
		}
		if ((*other)->name == dock.name)
		{
			return refuse(SameDock, dock.name);
		}
		if (!(*other)->docked)
		{
			return refuse(OtherNotDocked, (*other)->name);
		}
		break;
	}
	case Kind::Tab:
	{
		if (request.other.empty())
		{
			return refuse(NoTarget, dock.name);
		}
		const auto other{dockNamed(layout, request.other)};
		if (!other)
		{
			return std::unexpected(other.error());
		}
		if ((*other)->name == dock.name)
		{
			return refuse(SameDock, dock.name);
		}
		if (!(*other)->open || (*other)->autoHidden)
		{
			return refuse(OtherNotShown, (*other)->name);
		}
		if (std::ranges::contains(dock.tabs, (*other)->name))
		{
			return refuse(TabbedAlready, dock.name, std::format("with '{}'", (*other)->name));
		}
		break;
	}
	case Kind::SelectTab:
		if (!dock.open)
		{
			return refuse(Closed, dock.name);
		}
		if (dock.tabs.empty())
		{
			return refuse(NotTabbed, dock.name);
		}
		if (dock.currentTab)
		{
			return refuse(CurrentTabAlready, dock.name);
		}
		break;
	case Kind::AutoHide:
		if (!dock.open)
		{
			return refuse(Closed, dock.name);
		}
		if (dock.autoHidden)
		{
			return refuse(AutoHiddenAlready, dock.name, std::format("at the {}", to_std_sv(*dock.autoHidden)));
		}
		if (dock.floating)
		{
			return refuse(Floating, dock.name);
		}
		break;
	case Kind::Restore:
		if (!dock.open)
		{
			return refuse(Closed, dock.name);
		}
		if (!dock.floating && !dock.autoHidden)
		{
			return refuse(NotFloatingNorAutoHidden, dock.name);
		}
		break;
	case Kind::Resize:
		if (!request.width && !request.height)
		{
			return refuse(NoSize, dock.name);
		}
		if (request.width.value_or(1) <= 0 || request.height.value_or(1) <= 0)
		{
			return refuse(BadSize, dock.name);
		}
		if (!dock.open)
		{
			return refuse(Closed, dock.name);
		}
		if (dock.autoHidden)
		{
			return refuse(AutoHidden, dock.name);
		}
		break;
	case Kind::Reset:
		break;
	}
	return {};
}

std::shared_ptr<DockStateMachine> DockStateMachine::instance()
{
	static const std::shared_ptr<DockStateMachine> machine{std::make_shared<DockStateMachine>()};
	return machine;
}

void DockStateMachine::attach(DocksPresenter presenter, DockLayout docks)
{
	auto transaction{m_machine.transaction()};
	transaction.data().presenter = presenter;
	transaction.data().docks = std::move(docks);
}

void DockStateMachine::detach(DocksPresenter presenter)
{
	auto transaction{m_machine.transaction()};
	if (transaction.data().presenter == presenter)
	{
		transaction.data().presenter = {};
		transaction.data().docks.clear();
	}
}

MachineFireResult DockStateMachine::request(Request request, EventSource source)
{
	const Event event{eventFor(request.kind)};

	DocksPresenter presenter;
	{
		auto transaction{m_machine.transaction()};
		if (!transaction.accepts(event))
		{
			return transaction.refuse(event);
		}
		DocksMachine::Data& data{transaction.data()};
		if (!data.presenter)
		{
			return {false, std::string(to_std_sv(DockRequestError::NoLayout))};
		}
		// The window reports its docks once they settle after it opens (QtDockControl); a request
		// before that would be checked against no docks at all.
		if (data.docks.empty())
		{
			return {false, std::string(to_std_sv(DockRequestError::LayoutUnknown))};
		}
		if (const auto checked{checkDockRequest(data.docks, request)}; !checked)
		{
			return {false, std::format("{}: {}", m_machine.eventName(event), checked.error().message())};
		}

		data.request = std::string(m_machine.eventName(event));
		if (!request.dock.empty())
		{
			data.request += std::format(" '{}'", request.dock);
		}
		data.error.clear();
		data.outcome.clear();
		transaction.move(event, source);
		presenter = data.presenter;
	}
	presenter.applyToDocks(request);
	return {true, std::format("the window is on it ({}): until idle", m_machine.eventName(event))};
}

void DockStateMachine::report(Event event, DockLayout docks, const std::string& outcome, const std::string& error)
{
	auto transaction{m_machine.transaction()};
	DocksMachine::Data& data{transaction.data()};
	data.docks = std::move(docks);
	if (!transaction.accepts(event))
	{
		return;	   // no request of an agent's: the docks are as the window says, all the same
	}
	data.request.clear();
	data.outcome = outcome;
	data.error = error;
	transaction.move(event);
}

void DockStateMachine::applied(DockLayout docks, const std::string& outcome)
{
	report(Event::Applied, std::move(docks), outcome, {});
}

void DockStateMachine::failed(DockLayout docks, const std::string& why)
{
	report(Event::Failed, std::move(docks), {}, why);
}

void DockStateMachine::changed(DockLayout docks)
{
	auto transaction{m_machine.transaction()};
	DocksMachine::Data& data{transaction.data()};
	if (data.docks == docks)
	{
		return;
	}
	const bool first{data.docks.empty()};	 // the window's docks, as it starts: nobody changed them
	data.docks = std::move(docks);
	// While an agent's request is carried out, its report follows with the docks it led to.
	if (!first && transaction.accepts(Event::Changed))
	{
		transaction.move(Event::Changed, EventSource::Ui);
	}
}

MachineFireResult DockStateMachine::fire(const std::string& event, const std::string& args, EventSource source)
{
	namespace json = dock_machine_json;
	using Kind = Request::Kind;

	const std::optional<Event> control{m_machine.controlNamed(event)};
	if (!control)
	{
		return m_machine.unknownEvent(event);
	}
	Request request;
	switch (*control)
	{
	case Event::Show:
	case Event::Close:
	case Event::Float:
	case Event::SelectTab:
	case Event::AutoHide:
	case Event::Restore:
	{
		const auto parsed{parseArgs<json::DockArgs>(event, args, R"({"dock": "Graph"})")};
		if (!parsed)
		{
			return {false, parsed.error()};
		}
		constexpr std::array kinds {
			std::pair {Event::Show, Kind::Show},
			std::pair {Event::Close, Kind::Close},
			std::pair {Event::Float, Kind::Float},
			std::pair {Event::SelectTab, Kind::SelectTab},
			std::pair {Event::AutoHide, Kind::AutoHide},
			std::pair {Event::Restore, Kind::Restore}};
		request.kind = std::ranges::find(kinds, *control, &std::pair<Event, Kind>::first)->second;
		request.dock = parsed->dock;
		break;
	}
	case Event::Dock:
	{
		const auto parsed{parseArgs<json::DockBesideArgs>(event, args, R"({"dock": "Status", "to": "right", "of": "Code"})")};
		if (!parsed)
		{
			return {false, parsed.error()};
		}
		request.kind = Kind::Dock;
		request.dock = parsed->dock;
		request.other = parsed->of;
		if (!parsed->to.empty())
		{
			request.to = dockSideNamed(parsed->to);
			if (!request.to)
			{
				return {false, std::format("dock: `to` is left, right, top or bottom, not '{}'", parsed->to)};
			}
		}
		break;
	}
	case Event::Tab:
	{
		const auto parsed{parseArgs<json::TabArgs>(event, args, R"({"dock": "Status", "into": "Code"})")};
		if (!parsed)
		{
			return {false, parsed.error()};
		}
		request.kind = Kind::Tab;
		request.dock = parsed->dock;
		request.other = parsed->into;
		break;
	}
	case Event::Resize:
	{
		const auto parsed{parseArgs<json::ResizeArgs>(event, args, R"({"dock": "Graph", "width": 400, "height": 300})")};
		if (!parsed)
		{
			return {false, parsed.error()};
		}
		request.kind = Kind::Resize;
		request.dock = parsed->dock;
		request.width = parsed->width;
		request.height = parsed->height;
		break;
	}
	case Event::Reset:
	{
		if (const auto parsed{parseArgs<machine_args::None>(event, args)}; !parsed)
		{
			return {false, parsed.error()};
		}
		request.kind = Kind::Reset;
		break;
	}
	default:
		return m_machine.unknownEvent(event);
	}
	return this->request(std::move(request), source);
}

std::string DocksMachine::context(State state, const Data& data)
{
	namespace json = dock_machine_json;
	json::DocksContext context;
	for (const DockState& dock: data.docks)
	{
		json::DockEntry entry;
		entry.name = dock.name;
		entry.open = dock.open;
		entry.visible = dock.visible;
		entry.floating = dock.floating;
		if (dock.autoHidden)
		{
			entry.auto_hidden = std::string(to_std_sv(*dock.autoHidden));
			entry.overlay = dock.overlay;
		}
		if (dock.location)
		{
			entry.location = std::string(to_std_sv(*dock.location));
		}
		if (dock.docked)
		{
			entry.x = dock.docked->x;
			entry.y = dock.docked->y;
		}
		if (dock.open && dock.width > 0)
		{
			entry.width = dock.width;
			entry.height = dock.height;
		}
		if (!dock.tabs.empty())
		{
			entry.tabs = dock.tabs;
			entry.current_tab = dock.currentTab;
		}
		context.docks.push_back(std::move(entry));
	}
	if (state == State::Applying && !data.request.empty())
	{
		context.applying = data.request;
	}
	if (!data.error.empty())
	{
		context.error = data.error;
	}
	if (!data.outcome.empty())
	{
		context.outcome = data.outcome;
	}
	return glz::write_json(context).value_or("{}");
}
