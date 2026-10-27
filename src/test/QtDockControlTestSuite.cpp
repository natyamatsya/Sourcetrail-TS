#include "Catch2.hpp"

#include "DockStateMachine.h"
#include "QtDockChrome.h"
#include "QtDockControl.h"

#include <QApplication>
#include <QCursor>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QPointer>

#include <kddockwidgets/Config.h>
#include <kddockwidgets/KDDockWidgets.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/core/DropArea.h>
#include <kddockwidgets/core/DropIndicatorOverlay.h>
#include <kddockwidgets/core/Layout.h>
#include <kddockwidgets/core/MainWindow.h>
#include <kddockwidgets/qtcommon/View.h>
#include <kddockwidgets/qtwidgets/views/DockWidget.h>
#include <kddockwidgets/qtwidgets/views/MainWindow.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>

namespace
{
// Sourcetrail's main window, as far as its docks go: Search across the top, Graph and Code side
// by side, Status across the bottom, made of QtDockChrome's cards.
class DockWindow
{
public:
	DockWindow()
	{
		m_window->resize(1000, 700);
		m_window->show();
		for (const char* name: {"Search", "Graph", "Code", "Status"})
		{
			auto* dock{new KDDockWidgets::QtWidgets::DockWidget(QString::fromLatin1(name))};
			dock->setTitle(QString::fromLatin1(name));
			dock->setWidget(new QWidget);
			m_docks.push_back(dock);
			control.addDock(*dock);
		}
		classicLayout();
	}

	~DockWindow()
	{
		for (KDDockWidgets::QtWidgets::DockWidget* dock: m_docks)
		{
			delete dock;
		}
	}

	DockWindow(const DockWindow&) = delete;
	DockWindow& operator=(const DockWindow&) = delete;

	KDDockWidgets::Core::MainWindow& mainWindow() const
	{
		return *m_window->mainWindow();
	}

	QWidget& widget() const
	{
		return *m_window;
	}

	// Does `request` and lets the layout settle: what it led to.
	std::expected<std::string, DockApplyFailure> apply(const DockRequest& request)
	{
		if (const auto applied{control.apply(request)}; !applied)
		{
			return std::unexpected(applied.error());
		}
		QApplication::processEvents();
		return QtDockControl::outcome(request, control.layout());
	}

	DockState dock(std::string_view name) const
	{
		const DockLayout docks{control.layout()};
		return *std::ranges::find(docks, name, &DockState::name);
	}

private:
	void classicLayout()
	{
		m_window->addDockWidget(m_docks[1], KDDockWidgets::Location_OnLeft);
		m_window->addDockWidget(m_docks[2], KDDockWidgets::Location_OnRight, m_docks[1]);
		m_window->addDockWidget(m_docks[0], KDDockWidgets::Location_OnTop);
		m_window->addDockWidget(m_docks[3], KDDockWidgets::Location_OnBottom);
		QApplication::processEvents();
	}

	const std::unique_ptr<KDDockWidgets::QtWidgets::MainWindow> m_window{
		std::make_unique<KDDockWidgets::QtWidgets::MainWindow>(QStringLiteral("DockControlTest"))};
	std::vector<KDDockWidgets::QtWidgets::DockWidget*> m_docks;

public:
	DockStateMachine machine;
	QtDockControl control{*m_window, machine, [this]() { classicLayout(); }};
};

// An agent's mouse event, as agent control sends one: not spontaneous, to the widget that
// grabbed the mouse if one did, and without moving the platform's pointer.
void sendMouse(QWidget* receiver, QEvent::Type type, QPoint global, Qt::MouseButton button, Qt::MouseButtons buttons)
{
	QWidget* const target{QWidget::mouseGrabber() != nullptr ? QWidget::mouseGrabber() : receiver};
	QMouseEvent event{type, target->mapFromGlobal(QPointF{global}), QPointF{global}, button, buttons, Qt::NoModifier};
	QCoreApplication::sendEvent(target, &event);
	QApplication::processEvents();
}

// A drag from `from` to `to` in steps, on `receiver`.
void drag(QWidget* receiver, QPoint from, QPoint to, bool release = true)
{
	sendMouse(receiver, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
	constexpr int steps{10};
	for (int step{1}; step <= steps; ++step)
	{
		sendMouse(receiver, QEvent::MouseMove, from + (to - from) * step / steps, Qt::NoButton, Qt::LeftButton);
	}
	if (release)
	{
		sendMouse(receiver, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
	}
}

// The card `name` is shown in.
QWidget* cardOf(const QWidget& window, const QString& name)
{
	for (QWidget* card: window.findChildren<QWidget*>(QStringLiteral("dock_card")))
	{
		const auto docks{card->findChildren<KDDockWidgets::QtWidgets::DockWidget*>()};
		if (card->isVisible() && std::ranges::any_of(docks, [&](const auto* dock) { return dock->uniqueName() == name; }))
		{
			return card;
		}
	}
	return nullptr;
}

void waitFor(const std::function<bool()>& done)
{
	QElapsedTimer timer;
	timer.start();
	while (!done() && timer.elapsed() < 2000)
	{
		QApplication::processEvents(QEventLoop::AllEvents, 20);
	}
}

void setUpApplication()
{
	KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtWidgets);
	auto& config{KDDockWidgets::Config::self()};
	config.setViewFactory(new QtDockViewFactory());
	config.setFlags(
		config.flags() | KDDockWidgets::Config::Flag_AllowReorderTabs | KDDockWidgets::Config::Flag_AutoHideSupport |
		KDDockWidgets::Config::Flag_CloseOnlyCurrentTab);
}

char appName[]{"Sourcetrail_test"};
char* appArgv[]{appName, nullptr};
int appArgc{1};
}	 // namespace

TEST_CASE("the dock control arranges the docks with KDDockWidgets' own calls")
{
	using Kind = DockRequest::Kind;
	qputenv("QT_QPA_PLATFORM", "offscreen");
	const QApplication application{appArgc, appArgv};
	setUpApplication();
	{
		DockWindow window;
		REQUIRE(window.dock("Search").location == DockSide::Top);
		REQUIRE(window.dock("Graph").location == DockSide::Left);
		REQUIRE(window.dock("Code").location == DockSide::Right);
		REQUIRE(window.dock("Status").location == DockSide::Bottom);

		REQUIRE(window.apply({.kind = Kind::Tab, .dock = "Status", .other = "Code"}).has_value());
		REQUIRE(window.dock("Code").tabs == std::vector<std::string>{"Code", "Status"});
		REQUIRE(window.dock("Status").currentTab);
		REQUIRE(window.apply({.kind = Kind::SelectTab, .dock = "Code"}).has_value());
		REQUIRE_FALSE(window.dock("Status").visible);

		REQUIRE(window.apply({.kind = Kind::Dock, .dock = "Status", .to = DockSide::Right, .other = "Code"}).has_value());
		REQUIRE(window.dock("Status").tabs.empty());
		REQUIRE(window.dock("Status").location == DockSide::Right);
		REQUIRE(window.dock("Code").location == DockSide::Center);

		REQUIRE(window.apply({.kind = Kind::Float, .dock = "Graph"}).has_value());
		REQUIRE(window.dock("Graph").floating);
		REQUIRE_FALSE(window.dock("Graph").docked.has_value());
		REQUIRE(window.apply({.kind = Kind::Resize, .dock = "Graph", .width = 420, .height = 330}).has_value());
		REQUIRE(window.dock("Graph").width == 420);
		REQUIRE(window.dock("Graph").height == 330);
		REQUIRE(window.apply({.kind = Kind::Restore, .dock = "Graph"}).has_value());
		REQUIRE(window.dock("Graph").location == DockSide::Left);

		REQUIRE(window.apply({.kind = Kind::AutoHide, .dock = "Search"}).has_value());
		REQUIRE(window.dock("Search").autoHidden == DockSide::Top);
		REQUIRE_FALSE(window.dock("Search").visible);
		REQUIRE(window.apply({.kind = Kind::Show, .dock = "Search"}).has_value());
		REQUIRE(window.dock("Search").overlay);
		REQUIRE(window.apply({.kind = Kind::Restore, .dock = "Search"}).has_value());
		REQUIRE(window.dock("Search").location == DockSide::Top);

		const int codeWidth{window.dock("Code").width};
		const auto resized{window.apply({.kind = Kind::Resize, .dock = "Code", .width = codeWidth + 80})};
		REQUIRE(resized.has_value());
		REQUIRE(window.dock("Code").width == codeWidth + 80);
		REQUIRE(*resized == std::format("'Code' is {} x {}", codeWidth + 80, window.dock("Code").height));

		REQUIRE(window.apply({.kind = Kind::Close, .dock = "Status"}).has_value());
		REQUIRE_FALSE(window.dock("Status").open);
		REQUIRE(window.apply({.kind = Kind::Show, .dock = "Status"}).has_value());
		REQUIRE(window.dock("Status").visible);

		// The machine hears of it through the control.
		REQUIRE(window.machine.fire("tab", R"({"dock":"Graph","into":"Code"})", EventSource::Agent).ok);
		waitFor([&]() { return window.machine.snapshot().state == "idle"; });
		const MachineSnapshot snapshot{window.machine.snapshot()};
		REQUIRE(snapshot.state == "idle");
		REQUIRE(snapshot.context.find(R"("outcome":"'Graph' is tabbed into 'Code'")") != std::string::npos);
		REQUIRE(snapshot.context.find(R"("tabs":["Code","Graph"])") != std::string::npos);
	}
	QApplication::processEvents();
}

TEST_CASE("an agent's mouse drags docks by their grips and resizes them by their gutters")
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
	const QApplication application{appArgc, appArgv};
	setUpApplication();
	{
		DockWindow window;
		QCursor::setPos(QPoint{1, 1});	  // the platform's pointer is elsewhere, and stays there

		// The gutter between Graph and Code, dragged 60 px to the right.
		QWidget* const graph{cardOf(window.widget(), QStringLiteral("Graph"))};
		REQUIRE(graph != nullptr);
		const QPoint gutterPoint{graph->mapTo(&window.widget(), QPoint{graph->width() + QtDockChrome::gutterWidth / 2, graph->height() / 2})};
		QWidget* const gutter{window.widget().childAt(gutterPoint)};
		REQUIRE(gutter != nullptr);
		REQUIRE(gutter->objectName() == QStringLiteral("dock_gutter"));
		const int graphWidth{window.dock("Graph").width};
		const QPoint pressed{window.widget().mapToGlobal(gutterPoint)};
		drag(gutter, pressed, pressed + QPoint{60, 0});
		REQUIRE(std::abs(window.dock("Graph").width - (graphWidth + 60)) <= QtDockChrome::gutterWidth);

		// The machine follows, once the person's change settled.
		waitFor([&]() { return window.machine.snapshot().revision > 0; });
		REQUIRE(window.machine.snapshot().revision > 0);

		// Status, by the grip of its card, over Code, which shows the drop indicators, then onto the
		// center one: Status is a tab of Code's.
		QWidget* const status{cardOf(window.widget(), QStringLiteral("Status"))};
		QWidget* const code{cardOf(window.widget(), QStringLiteral("Code"))};
		REQUIRE(status != nullptr);
		REQUIRE(code != nullptr);
		QWidget* grip{nullptr};
		for (QWidget* candidate: status->findChildren<QWidget*>(QStringLiteral("dock_grip")))
		{
			grip = candidate->isVisible() ? candidate : grip;
		}
		REQUIRE(grip != nullptr);
		const QPoint from{grip->mapToGlobal(grip->rect().center())};
		drag(grip, from, code->mapToGlobal(code->rect().center()), false);
		KDDockWidgets::Core::DropIndicatorOverlay* const indicators{window.mainWindow().dropArea()->dropIndicatorOverlay()};
		const QPoint center{indicators->posForIndicator(KDDockWidgets::DropLocation_Center)};
		sendMouse(grip, QEvent::MouseMove, center, Qt::NoButton, Qt::LeftButton);
		REQUIRE(indicators->currentDropLocation() == KDDockWidgets::DropLocation_Center);
		sendMouse(grip, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
		REQUIRE(window.dock("Status").tabs == std::vector<std::string>{"Code", "Status"});
		REQUIRE_FALSE(window.dock("Status").floating);
		REQUIRE(QCursor::pos() == QPoint{1, 1});
	}
	QApplication::processEvents();
}
