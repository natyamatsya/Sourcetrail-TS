#include "Catch2.hpp"

#include "QtUiControl.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QElapsedTimer>
#include <QMenu>
#include <QMouseEvent>
#include <QStyleHints>
#include <QWidget>
#include <QWindow>

#include <array>
#include <format>
#include <memory>
#include <string>
#include <vector>

// How a platform plugin hands Qt a click (QtGui exports it; QTest declares it the same way):
// the event goes through QWidgetWindow as a person's does, with its popup routing, its
// mouse grab and its context-menu event.
Q_GUI_EXPORT void qt_handleMouseEvent(
	QWindow* window,
	const QPointF& local,
	const QPointF& global,
	Qt::MouseButtons state,
	Qt::MouseButton button,
	QEvent::Type type,
	Qt::KeyboardModifiers mods,
	int timestamp);

namespace
{
struct OffscreenPlatform
{
	OffscreenPlatform()
	{
		qputenv("QT_QPA_PLATFORM", "offscreen");
	}
};

class OffscreenApplication: private OffscreenPlatform
{
private:
	int m_argc{1};
	char m_name[17]{"Sourcetrail_test"};
	std::array<char*, 2> m_argv{m_name, nullptr};
	QApplication m_application{m_argc, m_argv.data()};
};

// Records the mouse and context-menu events the probe's widgets receive, as
// "receiver type button", in the order they arrive.
class Recorder: public QObject
{
public:
	std::vector<std::string> events;

	bool eventFilter(QObject* watched, QEvent* event) override
	{
		// Widgets only: the platform's path passes the window's QWindow first, which is plumbing.
		if (!watched->isWidgetType() || !watched->objectName().startsWith(QLatin1String("probe")))
		{
			return false;
		}
		const std::string receiver{watched->objectName().toStdString()};
		switch (event->type())
		{
		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonRelease:
		case QEvent::MouseButtonDblClick:
		{
			const auto* mouse{static_cast<QMouseEvent*>(event)};
			const char* type{event->type() == QEvent::MouseButtonPress ? "press"
					: event->type() == QEvent::MouseButtonRelease	  ? "release"
																	  : "double-click"};
			const char* button{mouse->button() == Qt::RightButton ? "right" : "left"};
			events.push_back(std::format("{} {} {}", receiver, type, button));
			break;
		}
		case QEvent::ContextMenu:
			events.push_back(receiver + " context-menu");
			break;
		default:
			break;
		}
		return false;
	}
};

// A widget that opens a menu on a context-menu event, as the code view does (without exec(),
// which would block the test).
class Probe: public QWidget
{
public:
	explicit Probe(QWidget* parent): QWidget(parent), m_menu(new QMenu(this))
	{
		setObjectName(QStringLiteral("probe"));
		m_menu->setObjectName(QStringLiteral("probe_menu"));
		m_menu->addAction(QStringLiteral("Something"));
	}

	QMenu& menu()
	{
		return *m_menu;
	}

protected:
	void contextMenuEvent(QContextMenuEvent* event) override
	{
		m_menu->popup(event->globalPos());
	}

private:
	QMenu* m_menu;
};

// A window with the probe in it, and the recorder watching the application.
struct Scene
{
	Scene()
	{
		window.setObjectName(QStringLiteral("probe_window"));
		window.resize(300, 200);
		probe = new Probe(&window);
		probe->setGeometry(50, 50, 200, 100);
		window.show();
		qApp->installEventFilter(&recorder);
		QApplication::processEvents();
	}

	~Scene()
	{
		qApp->removeEventFilter(&recorder);
	}

	QWidget window;
	Probe* probe{nullptr};
	Recorder recorder;
};

int s_timestamp{1000};

// A person's click at `global`, as the platform delivers it: press, then release.
void physicalClick(QWidget& window, QPoint global, Qt::MouseButton button)
{
	QWindow* const handle{window.windowHandle()};
	const QPointF local{handle->mapFromGlobal(QPointF{global})};
	qt_handleMouseEvent(handle, local, QPointF{global}, button, button, QEvent::MouseButtonPress, Qt::NoModifier, s_timestamp += 500);
	QApplication::processEvents();
	qt_handleMouseEvent(handle, local, QPointF{global}, Qt::NoButton, button, QEvent::MouseButtonRelease, Qt::NoModifier, s_timestamp += 500);
	QApplication::processEvents();
}

// The same click from an agent, through the bridge's input (ui_input at a point).
void agentClick(QPoint global, Qt::MouseButton button)
{
	const std::int32_t code{button == Qt::RightButton ? 2 : 1};
	utility::qt::InputEvent press{.kind = utility::qt::InputKind::MousePress, .x = global.x(), .y = global.y(), .button = code};
	utility::qt::InputEvent release{press};
	release.kind = utility::qt::InputKind::MouseRelease;
	bool delivered{false};
	const utility::qt::ControlResult result{
		utility::qt::sendInputAt(global.x(), global.y(), {press, release}, [&delivered]() { delivered = true; })};
	REQUIRE(result.ok);
	QElapsedTimer timer;
	timer.start();
	while (!delivered && timer.elapsed() < 2000)
	{
		QApplication::processEvents(QEventLoop::AllEvents, 20);
	}
	REQUIRE(delivered);
	QApplication::processEvents();
}

// The events a click causes, made by `click`, in a fresh scene; `before` sets the scene up
// (opens the menu) and is not recorded.
std::vector<std::string> eventsOf(
	const std::function<void(Scene&)>& before, const std::function<void(Scene&)>& click)
{
	Scene scene;
	before(scene);
	scene.recorder.events.clear();
	click(scene);
	scene.probe->menu().hide();
	return scene.recorder.events;
}
}	 // namespace

TEST_CASE("an agent's click reaches the widgets as a person's does")
{
	const OffscreenApplication application;
	const auto nothing{[](Scene&) {}};
	const auto onProbe{[](Scene& scene) { return scene.probe->mapToGlobal(QPoint{100, 50}); }};

	SECTION("a left click")
	{
		const auto physical{eventsOf(nothing, [&](Scene& scene) { physicalClick(scene.window, onProbe(scene), Qt::LeftButton); })};
		const auto agent{eventsOf(nothing, [&](Scene& scene) { agentClick(onProbe(scene), Qt::LeftButton); })};
		INFO("physical: " << std::format("{}", physical));
		CHECK(agent == physical);
	}

	for (const Qt::ContextMenuTrigger trigger: {Qt::ContextMenuTrigger::Press, Qt::ContextMenuTrigger::Release})
	{
		// Platforms differ: macOS and Linux open the context menu on the press, Windows on the
		// release.
		QGuiApplication::styleHints()->setContextMenuTrigger(trigger);
		DYNAMIC_SECTION("a right click opening a menu, context menu on " << (trigger == Qt::ContextMenuTrigger::Press ? "press" : "release"))
		{
			const auto physical{eventsOf(nothing, [&](Scene& scene) { physicalClick(scene.window, onProbe(scene), Qt::RightButton); })};
			const auto agent{eventsOf(nothing, [&](Scene& scene) { agentClick(onProbe(scene), Qt::RightButton); })};
			INFO("physical: " << std::format("{}", physical));
			CHECK(agent == physical);
		}
	}

	SECTION("a left click beside an open menu")
	{
		const auto openMenu{[](Scene& scene) {
			scene.probe->menu().popup(scene.probe->mapToGlobal(QPoint{150, 80}));
			QApplication::processEvents();
		}};
		const auto beside{[](Scene& scene) { return scene.probe->mapToGlobal(QPoint{10, 10}); }};
		const auto physical{eventsOf(openMenu, [&](Scene& scene) { physicalClick(scene.window, beside(scene), Qt::LeftButton); })};
		const auto agent{eventsOf(openMenu, [&](Scene& scene) { agentClick(beside(scene), Qt::LeftButton); })};
		INFO("physical: " << std::format("{}", physical));
		CHECK(agent == physical);
	}
}
