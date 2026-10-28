#ifndef DOCK_STATE_MACHINE_H
#define DOCK_STATE_MACHINE_H

#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ConcurrentStateMachine.h"
#include "Presenter.h"

// A side of the layout, or of a dock: where a dock is docked (center: nowhere near an edge),
// the side bar an auto-hidden dock waits in, the side to dock a dock at.
enum class DockSide : std::uint8_t
{
	Left,
	Right,
	Top,
	Bottom,
	Center
};

constexpr std::string_view to_std_sv(DockSide side) noexcept
{
	using enum DockSide;
	switch (side)
	{
	case Left: return "left";
	case Right: return "right";
	case Top: return "top";
	case Bottom: return "bottom";
	case Center: return "center";
	}
	return "center";
}

// The side called `name` ("left", ...), if there is one.
std::optional<DockSide> dockSideNamed(std::string_view name);

// A rectangle in the layout's coordinates: 0,0 is its top-left.
struct DockRect
{
	int x{0};
	int y{0};
	int width{0};
	int height{0};

	friend bool operator==(const DockRect&, const DockRect&) = default;
};

// Where `dock` is in a layout of `width` x `height`, by the edges it reaches (within `slack`):
// the left or right side if it reaches one of them only; across the whole width, the top or the
// bottom if it reaches one of them only; else the center. A dock across the top is top, the left
// one of three beside each other in the middle is left, and the middle one center.
DockSide dockSideInLayout(const DockRect& dock, int width, int height, int slack);

// One dock (Search, Graph, Code, Status, ...) as the window arranges it.
struct DockState
{
	std::string name;
	bool open{false};	 // part of the layout: shown, a tab behind another, or auto-hidden
	bool visible{false};	// on the screen now
	bool floating{false};	// in a window of its own
	std::optional<DockSide> autoHidden;	   // the side bar it waits in
	bool overlay{false};	// auto-hidden and shown over the others meanwhile
	std::optional<DockRect> docked;	   // where it is in the window's layout
	std::optional<DockSide> location;	 // docked: the layout's side it is at
	std::vector<std::string> tabs;	  // the docks tabbed with it, itself included, in order; empty when alone
	bool currentTab{false};
	int width{0};	 // open: its card's size (a floating one: its window's)
	int height{0};

	friend bool operator==(const DockState&, const DockState&) = default;
};

using DockLayout = std::vector<DockState>;

// What an agent asks of the docks; the window does it on the GUI thread, with KDDockWidgets'
// own calls (the ones its buttons and drags use), and reports the layout it led to.
struct DockRequest
{
	enum class Kind : std::uint8_t
	{
		Show,	 // a closed dock opens where it was, an auto-hidden one shows over the others, a tab becomes current
		Close,
		Float,	  // into a window of its own, as the float button does
		Dock,	 // at the side `to` of the dock `other`, or of the whole layout
		Tab,	// into the dock `other`'s tabs
		SelectTab,	  // its tab becomes the current one
		AutoHide,	 // into the side bar, as the auto-hide button does
		Restore,	// out of the side bar, or a floating one back where it was docked
		Resize,	   // its card to `width` and/or `height`
		Reset	 // View > Reset Layout
	};

	Kind kind{Kind::Show};
	std::string dock;
	std::optional<DockSide> to;	   // Dock
	std::string other;	  // Dock: beside it (empty: the layout's edge); Tab: into it
	std::optional<int> width;	 // Resize
	std::optional<int> height;
};

constexpr std::string_view to_std_sv(DockRequest::Kind kind) noexcept
{
	using enum DockRequest::Kind;
	switch (kind)
	{
	case Show: return "show";
	case Close: return "close";
	case Float: return "float";
	case Dock: return "dock";
	case Tab: return "tab";
	case SelectTab: return "select_tab";
	case AutoHide: return "auto_hide";
	case Restore: return "restore";
	case Resize: return "resize";
	case Reset: return "reset";
	}
	return "?";
}

// Why a dock request is refused before the window is asked.
enum class DockRequestError : std::uint8_t
{
	NoLayout,
	LayoutUnknown,
	NoDock,
	UnknownDock,
	SameDock,
	ShownAlready,
	ClosedAlready,
	LastDock,
	Closed,
	FloatingAlready,
	AutoHidden,
	AutoHiddenAlready,
	NotFloatingNorAutoHidden,
	Floating,
	NotTabbed,
	CurrentTabAlready,
	TabbedAlready,
	OtherNotDocked,
	OtherNotShown,
	NoTarget,
	NoSide,
	NoSize,
	BadSize
};

constexpr std::string_view to_std_sv(DockRequestError error) noexcept
{
	using enum DockRequestError;
	switch (error)
	{
	case NoLayout: return "there is no window with docks";
	case LayoutUnknown: return "the window has not reported its docks yet: ask again in a moment";
	case NoDock: return "needs `dock`, the name of one";
	case UnknownDock: return "is no dock";
	case SameDock: return "cannot go beside or into itself";
	case ShownAlready: return "is shown already";
	case ClosedAlready: return "is closed already";
	case LastDock: return "is the last dock open: closing it would leave no dock";
	case Closed: return "is closed: show it first";
	case FloatingAlready: return "is floating already";
	case AutoHidden: return "is auto-hidden: restore it first";
	case AutoHiddenAlready: return "is auto-hidden already";
	case NotFloatingNorAutoHidden: return "is neither floating nor auto-hidden: nothing to restore";
	case Floating: return "is floating: only a docked dock can be auto-hidden";
	case NotTabbed: return "is not tabbed with another dock";
	case CurrentTabAlready: return "is the current tab already";
	case TabbedAlready: return "is tabbed there already";
	case OtherNotDocked: return "is not docked in the window (closed, floating or auto-hidden): nothing to dock beside";
	case OtherNotShown: return "is closed or auto-hidden: nothing to tab into";
	case NoTarget: return "needs `into`, the dock to tab it into";
	case NoSide: return "needs `to`: left, right, top or bottom";
	case NoSize: return "needs a width or a height";
	case BadSize: return "takes sizes above 0";
	}
	return "cannot be done";
}

// A refused request: what was wrong, the dock it concerns, and what there is to choose from
// when that is the trouble.
struct DockRequestFailure
{
	DockRequestError error{DockRequestError::UnknownDock};
	std::string dock;
	std::string detail;

	// For the agent: "'Grpah' is no dock (there are Search, Graph, Code, Status)".
	[[nodiscard]] std::string message() const
	{
		const std::string subject{dock.empty() ? std::string() : std::format("'{}' ", dock)};
		return detail.empty() ? std::format("{}{}", subject, to_std_sv(error))
							  : std::format("{}{} ({})", subject, to_std_sv(error), detail);
	}
};

// Whether `request` makes sense in `layout`: what the machine can tell before the window is
// asked.
std::expected<void, DockRequestFailure> checkDockRequest(const DockLayout& layout, const DockRequest& request);

// Arranges the docks for an agent: applyToDocks(request) does the request on the GUI thread
// and reports it (DockStateMachine::applied / failed).
template <typename T>
concept PresentsDocks = requires(T& presenter, const DockRequest& request) { presenter.applyToDocks(request); };

struct DocksPresenterOperations
{
	void (*apply)(void* presenter, const DockRequest& request);
};

// Any PresentsDocks, by reference (Presenter.h).
class DocksPresenter: public Presenter<DocksPresenterOperations>
{
public:
	using Presenter::Presenter;

	template <PresentsDocks T>
		requires(!std::derived_from<T, Presenter>)	  // a handle is copied, not referred to
	DocksPresenter(T& presenter): Presenter(presenter, &operations<T>)
	{
	}

	void applyToDocks(const DockRequest& request) const
	{
		m_operations->apply(m_presenter, request);
	}

private:
	template <PresentsDocks T>
	static constexpr DocksPresenterOperations operations {
		[](void* presenter, const DockRequest& request) { static_cast<T*>(presenter)->applyToDocks(request); }};
};

// The docks of the main window (Search, Graph, Code, Status, ...) and how they are arranged:
//
//   idle --show/close/float/dock/tab/select_tab/auto_hide/restore/resize/reset--> applying
//   applying --applied/failed--> idle;  idle --changed--> idle
//
// `applying` lasts while the window carries out an agent's request on the GUI thread; a
// request the machine can tell is wrong (an unknown dock, closing the last one) is refused
// before. The context lists the docks as the window last reported them. A person's docking
// (a drag, a gutter, a dock's buttons, the View menu) is `changed`, from the ui.
struct DocksMachine
{
	enum class State
	{
		Idle,
		Applying
	};
	enum class Event
	{
		Show,
		Close,
		Float,
		Dock,
		Tab,
		SelectTab,
		AutoHide,
		Restore,
		Resize,
		Reset,
		Applied,
		Failed,
		Changed
	};

	struct Data
	{
		DockLayout docks;	 // as the window last reported them
		std::string request;	// what applying carries out
		std::string error;	  // why the last request failed
		std::string outcome;	// what the last request did
		DocksPresenter presenter;
	};

	static constexpr std::string_view name = "docks";
	static constexpr State initial = State::Idle;
	static constexpr std::array states {named(State::Idle, "idle"), named(State::Applying, "applying")};
	static constexpr std::array events {
		control(Event::Show, "show"),
		control(Event::Close, "close"),
		control(Event::Float, "float"),
		control(Event::Dock, "dock"),
		control(Event::Tab, "tab"),
		control(Event::SelectTab, "select_tab"),
		control(Event::AutoHide, "auto_hide"),
		control(Event::Restore, "restore"),
		control(Event::Resize, "resize"),
		control(Event::Reset, "reset"),
		named(Event::Applied, "applied"),
		named(Event::Failed, "failed"),
		named(Event::Changed, "changed")};
	static constexpr auto transitions = std::to_array<StateMachine<State, Event>::Transition>({
		{State::Idle, Event::Show, State::Applying},
		{State::Idle, Event::Close, State::Applying},
		{State::Idle, Event::Float, State::Applying},
		{State::Idle, Event::Dock, State::Applying},
		{State::Idle, Event::Tab, State::Applying},
		{State::Idle, Event::SelectTab, State::Applying},
		{State::Idle, Event::AutoHide, State::Applying},
		{State::Idle, Event::Restore, State::Applying},
		{State::Idle, Event::Resize, State::Applying},
		{State::Idle, Event::Reset, State::Applying},
		{State::Idle, Event::Changed, State::Idle},
		{State::Applying, Event::Applied, State::Idle},
		{State::Applying, Event::Failed, State::Idle},
	});

	static std::string context(State state, const Data& data);
};

class DockStateMachine
{
public:
	using State = DocksMachine::State;
	using Request = DockRequest;

	static std::shared_ptr<DockStateMachine> instance();

	// The main window: arranges the docks for an agent, and reports how they are.
	void attach(DocksPresenter presenter, DockLayout docks);
	void detach(DocksPresenter presenter);

	// The controls: `request` to the window, from `source`.
	MachineFireResult request(Request request, EventSource source);

	// The window's side: the docks now, after an agent's request (what it did, or why it did
	// not), or after a person changed them (a move when they did change).
	void applied(DockLayout docks, const std::string& outcome);
	void failed(DockLayout docks, const std::string& why);
	void changed(DockLayout docks);

	MachineSnapshot snapshot() const
	{
		return m_machine.snapshot();
	}
	MachineFireResult fire(const std::string& event, const std::string& args, EventSource source);

private:
	using Machine = ConcurrentStateMachine<DocksMachine>;
	using Event = DocksMachine::Event;

	// Moves along `event` (applied, failed) to idle, with the window's `docks`.
	void report(Event event, DockLayout docks, const std::string& outcome, const std::string& error);

	Machine m_machine;
};

#endif	  // DOCK_STATE_MACHINE_H
