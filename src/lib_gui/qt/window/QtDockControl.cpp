#include "QtDockControl.h"

#include <algorithm>
#include <utility>

#include <QEvent>
#include <QWidget>

#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/core/FloatingWindow.h>
#include <kddockwidgets/core/Group.h>
#include <kddockwidgets/core/Layout.h>
#include <kddockwidgets/core/MainWindow.h>
#include <kddockwidgets/core/Platform.h>
#include <kddockwidgets/qtcommon/View.h>
#include <kddockwidgets/qtwidgets/views/DockWidget.h>
#include <kddockwidgets/qtwidgets/views/Group.h>
#include <kddockwidgets/qtwidgets/views/MainWindow.h>

#include "QtDockChrome.h"
#include "UiPost.h"

namespace
{
// How long a person's change may take to settle (a dock's buttons send several signals).
constexpr int settleMs{50};

std::optional<DockSide> sideOf(KDDockWidgets::SideBarLocation location)
{
	switch (location)
	{
		using enum KDDockWidgets::SideBarLocation;
	case North: return DockSide::Top;
	case East: return DockSide::Right;
	case West: return DockSide::Left;
	case South: return DockSide::Bottom;
	case None:
	case Last: return std::nullopt;
	}
	return std::nullopt;
}

KDDockWidgets::Location locationOf(DockSide side)
{
	switch (side)
	{
	case DockSide::Left: return KDDockWidgets::Location_OnLeft;
	case DockSide::Right: return KDDockWidgets::Location_OnRight;
	case DockSide::Top: return KDDockWidgets::Location_OnTop;
	case DockSide::Bottom: return KDDockWidgets::Location_OnBottom;
	case DockSide::Center: return KDDockWidgets::Location_None;
	}
	return KDDockWidgets::Location_None;
}

// The card `dock` is in: its group, when it is in one (an auto-hidden dock not shown is in none).
KDDockWidgets::Core::Group* groupOf(const KDDockWidgets::QtWidgets::DockWidget& dock)
{
	for (QWidget* widget{dock.parentWidget()}; widget != nullptr; widget = widget->parentWidget())
	{
		if (auto* group{qobject_cast<KDDockWidgets::QtWidgets::Group*>(widget)})
		{
			return group->asGroupController();
		}
		if (widget->isWindow())
		{
			break;
		}
	}
	return nullptr;
}

QWidget* widgetOf(KDDockWidgets::Core::View* view)
{
	return KDDockWidgets::QtCommon::View_qt::asQWidget(view);
}

const DockState* findDock(const DockLayout& docks, std::string_view name)
{
	const auto it{std::ranges::find(docks, name, &DockState::name)};
	return it == docks.end() ? nullptr : &*it;
}

std::unexpected<DockApplyFailure> notDone(const std::string& dock, std::string detail)
{
	return std::unexpected(DockApplyFailure{DockApplyError::NotDone, dock, std::move(detail)});
}

// Resizes `dock` in the layout to `target` along one axis: the far edge first (right, bottom),
// then the near one for what that could not give (a dock at the layout's far edge).
void resizeInLayout(KDDockWidgets::Core::DockWidget& dock, int target, bool horizontal)
{
	const auto extent{[&]() { return horizontal ? dock.sizeInLayout().width() : dock.sizeInLayout().height(); }};
	if (const int grow{target - extent()}; grow != 0)
	{
		horizontal ? dock.resizeInLayout(0, 0, grow, 0) : dock.resizeInLayout(0, 0, 0, grow);
	}
	if (const int grow{target - extent()}; grow != 0)
	{
		horizontal ? dock.resizeInLayout(grow, 0, 0, 0) : dock.resizeInLayout(0, grow, 0, 0);
	}
}
}	 // namespace

QtDockControl::QtDockControl(
	KDDockWidgets::QtWidgets::MainWindow& window, DockStateMachine& machine, std::function<void()> resetLayout)
	: m_window{window}
	, m_machine{machine}
	, m_resetLayout{std::move(resetLayout)}
{
	m_changes.setSingleShot(true);
	m_changes.setInterval(settleMs);
	connect(&m_changes, &QTimer::timeout, this, &QtDockControl::reportChange);
	m_window.installEventFilter(this);
	m_machine.attach(*this, {});
}

QtDockControl::~QtDockControl()
{
	m_machine.detach(*this);
}

void QtDockControl::addDock(KDDockWidgets::QtWidgets::DockWidget& dock)
{
	m_docks.emplace_back(&dock);
	dock.installEventFilter(this);
	connect(&dock, &KDDockWidgets::QtWidgets::DockWidget::isOpenChanged, this, &QtDockControl::layoutMayHaveChanged);
	connect(&dock, &KDDockWidgets::QtWidgets::DockWidget::isFloatingChanged, this, &QtDockControl::layoutMayHaveChanged);
	connect(&dock, &KDDockWidgets::QtWidgets::DockWidget::isCurrentTabChanged, this, &QtDockControl::layoutMayHaveChanged);
	connect(&dock, &QObject::destroyed, this, &QtDockControl::layoutMayHaveChanged);
	layoutMayHaveChanged();
}

DockLayout QtDockControl::layout() const
{
	KDDockWidgets::Core::MainWindow* const mainWindow{m_window.mainWindow()};
	const KDDockWidgets::Size layoutSize{mainWindow != nullptr ? mainWindow->layout()->layoutSize() : KDDockWidgets::Size{}};

	DockLayout docks;
	for (const QPointer<KDDockWidgets::QtWidgets::DockWidget>& view: m_docks)
	{
		if (view == nullptr)
		{
			continue;
		}
		KDDockWidgets::Core::DockWidget* const dock{view->dockWidget()};
		DockState state;
		state.name = dock->uniqueName().toStdString();
		state.autoHidden = dock->isInSideBar() ? sideOf(dock->sideBarLocation()) : std::nullopt;
		state.open = dock->isOpen() || state.autoHidden.has_value();
		state.visible = view->isVisible();
		state.overlay = dock->isOverlayed();

		KDDockWidgets::Core::FloatingWindow* const floating{dock->floatingWindow()};
		state.floating = state.open && floating != nullptr;
		KDDockWidgets::Core::Group* const group{state.open ? groupOf(*view) : nullptr};
		if (group != nullptr)
		{
			if (const auto tabs{group->dockWidgets()}; tabs.size() > 1)
			{
				for (const KDDockWidgets::Core::DockWidget* tab: tabs)
				{
					state.tabs.push_back(tab->uniqueName().toStdString());
				}
				state.currentTab = group->currentDockWidget() == dock;
			}
			const QSize size{widgetOf(group->view())->size()};
			state.width = size.width();
			state.height = size.height();
		}
		if (state.open && !state.floating && !state.autoHidden && dock->isInMainWindow())
		{
			const KDDockWidgets::Rect rect{dock->groupGeometry()};
			state.docked = DockRect{rect.x(), rect.y(), rect.width(), rect.height()};
			state.location = dockSideInLayout(
				*state.docked, layoutSize.width(), layoutSize.height(), 2 * QtDockChrome::gutterWidth);
		}
		if (state.floating)
		{
			const QSize size{widgetOf(floating->view())->size()};
			state.width = size.width();
			state.height = size.height();
		}
		docks.push_back(std::move(state));
	}
	return docks;
}

void QtDockControl::applyToDocks(const DockRequest& request)
{
	execution::qt::onUi(this, [this, request]() {
		if (const auto applied{apply(request)}; !applied)
		{
			m_machine.failed(layout(), applied.error().message());
			return;
		}
		// KDDockWidgets shows a floating window, lays out the rest, on later turns.
		QTimer::singleShot(0, this, [this, request]() {
			const DockLayout docks{layout()};
			if (const auto done{outcome(request, docks)})
			{
				m_machine.applied(docks, *done);
			}
			else
			{
				m_machine.failed(docks, done.error().message());
			}
		});
	});
}

std::expected<void, DockApplyFailure> QtDockControl::apply(const DockRequest& request)
{
	using Kind = DockRequest::Kind;
	KDDockWidgets::Core::MainWindow* const mainWindow{m_window.mainWindow()};
	if (request.kind == Kind::Reset)
	{
		m_resetLayout();
		return {};
	}
	KDDockWidgets::Core::DockWidget* const dock{KDDockWidgets::Core::DockWidget::byName(QString::fromStdString(request.dock))};
	KDDockWidgets::Core::DockWidget* const other{
		request.other.empty() ? nullptr : KDDockWidgets::Core::DockWidget::byName(QString::fromStdString(request.other))};
	if (dock == nullptr || mainWindow == nullptr)
	{
		return std::unexpected(DockApplyFailure{DockApplyError::DockGone, request.dock, {}});
	}
	if (!request.other.empty() && other == nullptr)
	{
		return std::unexpected(DockApplyFailure{DockApplyError::DockGone, request.other, {}});
	}

	switch (request.kind)
	{
	case Kind::Show:
		if (!dock->isOpen() && !dock->isInSideBar())
		{
			dock->open();
		}
		else if (dock->isInSideBar())
		{
			mainWindow->overlayOnSideBar(dock);
		}
		else
		{
			dock->setAsCurrentTab();
			dock->raise();
		}
		break;
	case Kind::Close:
		dock->close();
		break;
	case Kind::Float:
		dock->setFloating(true);
		break;
	case Kind::Dock:
		if (dock->isInSideBar())
		{
			dock->removeFromSideBar();
		}
		mainWindow->addDockWidget(dock, locationOf(request.to.value_or(DockSide::Center)), other);
		break;
	case Kind::Tab:
		if (dock->isInSideBar())
		{
			dock->removeFromSideBar();
		}
		other->addDockWidgetAsTab(dock);
		break;
	case Kind::SelectTab:
		dock->setAsCurrentTab();
		break;
	case Kind::AutoHide:
		mainWindow->moveToSideBar(dock);
		break;
	case Kind::Restore:
		if (dock->isInSideBar())
		{
			mainWindow->restoreFromSideBar(dock);
		}
		else
		{
			dock->setFloating(false);
		}
		break;
	case Kind::Resize:
		if (KDDockWidgets::Core::FloatingWindow* const floating{dock->floatingWindow()})
		{
			QWidget* const window{widgetOf(floating->view())};
			window->resize(request.width.value_or(window->width()), request.height.value_or(window->height()));
		}
		else
		{
			if (request.width)
			{
				resizeInLayout(*dock, *request.width, true);
			}
			if (request.height)
			{
				resizeInLayout(*dock, *request.height, false);
			}
		}
		break;
	case Kind::Reset:
		break;
	}
	return {};
}

std::expected<std::string, DockApplyFailure> QtDockControl::outcome(const DockRequest& request, const DockLayout& docks)
{
	using Kind = DockRequest::Kind;
	if (request.kind == Kind::Reset)
	{
		return "the layout is reset";
	}
	const DockState* const dock{findDock(docks, request.dock)};
	if (dock == nullptr)
	{
		return std::unexpected(DockApplyFailure{DockApplyError::DockGone, request.dock, {}});
	}
	const std::string& name{dock->name};
	switch (request.kind)
	{
	case Kind::Show:
		if (!dock->visible)
		{
			return notDone(name, "it is not shown");
		}
		return std::format("'{}' is shown", name);
	case Kind::Close:
		if (dock->open)
		{
			return notDone(name, "it is still open");
		}
		return std::format("'{}' is closed", name);
	case Kind::Float:
		if (!dock->floating)
		{
			return notDone(name, "it is not floating");
		}
		return std::format("'{}' floats", name);
	case Kind::Dock:
	{
		if (!dock->docked)
		{
			return notDone(name, "it is not docked in the window");
		}
		const std::string_view side{to_std_sv(request.to.value_or(DockSide::Center))};
		return request.other.empty() ? std::format("'{}' is docked at the {} of the layout", name, side)
									 : std::format("'{}' is docked {} of '{}'", name, side, request.other);
	}
	case Kind::Tab:
		if (!std::ranges::contains(dock->tabs, request.other))
		{
			return notDone(name, std::format("it is not tabbed with '{}'", request.other));
		}
		return std::format("'{}' is tabbed into '{}'", name, request.other);
	case Kind::SelectTab:
		if (!dock->currentTab)
		{
			return notDone(name, "it is not the current tab");
		}
		return std::format("'{}' is the current tab", name);
	case Kind::AutoHide:
		if (!dock->autoHidden)
		{
			return notDone(name, "it is not auto-hidden");
		}
		return std::format("'{}' is auto-hidden at the {}", name, to_std_sv(*dock->autoHidden));
	case Kind::Restore:
		if (dock->autoHidden || dock->floating)
		{
			return notDone(name, dock->autoHidden ? "it is still auto-hidden" : "it is still floating");
		}
		return std::format("'{}' is docked again", name);
	case Kind::Resize:
	{
		const bool reached{request.width.value_or(dock->width) == dock->width && request.height.value_or(dock->height) == dock->height};
		return std::format(
			"'{}' is {} x {}{}",
			name,
			dock->width,
			dock->height,
			reached ? "" : " (as near as the docks around it and their minimum sizes allow)");
	}
	case Kind::Reset:
		break;
	}
	return "done";
}

bool QtDockControl::eventFilter(QObject* watched, QEvent* event)
{
	switch (event->type())
	{
	case QEvent::Show:
	case QEvent::Hide:
	case QEvent::Resize:
	case QEvent::Move:
	case QEvent::ParentChange:
		layoutMayHaveChanged();
		break;
	default:
		break;
	}
	return QObject::eventFilter(watched, event);
}

void QtDockControl::layoutMayHaveChanged()
{
	if (const std::uint64_t cause{currentCause()};
		cause != 0 && std::ranges::none_of(m_causes, [cause](const CauseWork::Held& held) { return held.cause() == cause; }))
	{
		m_causes.emplace_back(cause);
	}
	if (!m_changes.isActive())
	{
		m_changes.start();
	}
}

void QtDockControl::reportChange()
{
	// In the middle of a drag (a card, a gutter) the layout is not what the person will leave. An
	// agent's drag may stop there, for a look at the drop indicators: its request has settled.
	if (KDDockWidgets::Core::Platform::hasInstance() && KDDockWidgets::Core::Platform::instance()->isLeftMouseButtonPressed())
	{
		m_causes.clear();
		m_changes.start();
		return;
	}
	const std::vector<CauseWork::Held> causes{std::exchange(m_causes, {})};
	const CauseScope scope{causes.empty() ? 0 : causes.front().cause()};
	m_machine.changed(layout());
}
