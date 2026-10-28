#include "Catch2.hpp"

#include "QtUiControl.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QPointer>
#include <QCursor>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QStyleHints>
#include <QWheelEvent>
#include <QWidget>
#include <QWindow>

#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// How a platform plugin hands Qt a person's input (QtGui exports these; QTest declares them the
// same way): through the window, with its popup routing, mouse grab, context-menu event, focus,
// shortcuts, enter and leave.
Q_GUI_EXPORT void qt_handleMouseEvent(
	QWindow* window,
	const QPointF& local,
	const QPointF& global,
	Qt::MouseButtons state,
	Qt::MouseButton button,
	QEvent::Type type,
	Qt::KeyboardModifiers mods,
	int timestamp);
Q_GUI_EXPORT void qt_handleKeyEvent(
	QWindow* window,
	QEvent::Type type,
	int key,
	Qt::KeyboardModifiers mods,
	const QString& text,
	bool autorep,
	ushort count);
Q_GUI_EXPORT void qt_handleWheelEvent(
	QWindow* window,
	const QPointF& local,
	const QPointF& global,
	QPoint pixelDelta,
	QPoint angleDelta,
	Qt::KeyboardModifiers mods,
	Qt::ScrollPhase phase);

namespace
{
using utility::qt::InputKind;

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

// Records the input events the probe's widgets receive, as "receiver type detail", in the
// order they arrive.
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
		const auto button = [](const QMouseEvent* mouse) { return mouse->button() == Qt::RightButton ? "right" : "left"; };
		switch (event->type())
		{
		case QEvent::MouseButtonPress:
			events.push_back(std::format("{} press {}", receiver, button(static_cast<QMouseEvent*>(event))));
			break;
		case QEvent::MouseButtonRelease:
			events.push_back(std::format("{} release {}", receiver, button(static_cast<QMouseEvent*>(event))));
			break;
		case QEvent::MouseButtonDblClick:
			events.push_back(std::format("{} double-click {}", receiver, button(static_cast<QMouseEvent*>(event))));
			break;
		case QEvent::MouseMove:
			events.push_back(receiver + " move");
			break;
		case QEvent::ContextMenu:
			events.push_back(receiver + " context-menu");
			break;
		case QEvent::Enter:
			events.push_back(receiver + " enter");
			break;
		case QEvent::Leave:
			events.push_back(receiver + " leave");
			break;
		case QEvent::Wheel:
			events.push_back(receiver + " wheel");
			break;
		case QEvent::KeyPress:
		case QEvent::KeyRelease:
		{
			const auto* key{static_cast<QKeyEvent*>(event)};
			events.push_back(std::format(
				"{} key-{} {}",
				receiver,
				event->type() == QEvent::KeyPress ? "press" : "release",
				QKeySequence(key->keyCombination()).toString().toStdString()));
			break;
		}
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

// A window with the probe, another widget, a line edit with the keyboard focus, and an action
// with a shortcut; the recorder watching the application.
struct Scene
{
	Scene()
	{
		window.setObjectName(QStringLiteral("probe_window"));
		window.resize(300, 220);
		probe = new Probe(&window);
		probe->setGeometry(50, 50, 200, 100);
		other = new QWidget(&window);
		other->setObjectName(QStringLiteral("probe_other"));
		other->setGeometry(50, 160, 100, 40);
		edit = new QLineEdit(&window);
		edit->setObjectName(QStringLiteral("probe_edit"));
		edit->setGeometry(160, 160, 100, 40);
		auto* action{new QAction(&window)};
		action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
		window.addAction(action);
		QObject::connect(action, &QAction::triggered, [this]() { recorder.events.emplace_back("probe_action triggered"); });
		window.show();
		window.activateWindow();
		edit->setFocus();
		qApp->installEventFilter(&recorder);
		QApplication::processEvents();
	}

	~Scene()
	{
		qApp->removeEventFilter(&recorder);
	}

	QWidget window;
	Probe* probe{nullptr};
	QWidget* other{nullptr};
	QLineEdit* edit{nullptr};
	Recorder recorder;
};

// One step of a gesture, at a point of the scene (global then).
struct Step
{
	InputKind kind;
	std::function<QPoint(Scene&)> at;
	Qt::MouseButton button{Qt::NoButton};
	int key{0};
	Qt::KeyboardModifiers modifiers{Qt::NoModifier};
	QString text;
};

int s_timestamp{100000};

// The steps as a person makes them, with a pointer the platform reports: it moves to each point
// first (QCursor::setPos, offscreen: the move, and enter and leave between windows, as an OS
// reports them), and the buttons and keys go to the window the platform gives them to: the
// press's window while a button is held, else the one under the pointer; keys to the focused
// window. A double click is the second press within the double-click interval of the first.
void physically(Scene& scene, const std::vector<Step>& steps)
{
	Qt::MouseButtons held;
	QPointer<QWindow> pressWindow;
	for (const Step& step: steps)
	{
		const QPoint global{step.at(scene)};
		const bool isKey{step.kind == InputKind::KeyPress || step.kind == InputKind::KeyRelease};
		if (!isKey && QCursor::pos() != global)
		{
			QCursor::setPos(global);
			QApplication::processEvents();	  // the move, enter and leave come as the pointer moves
		}
		QWindow* window{held != Qt::NoButton && pressWindow ? pressWindow.data() : QGuiApplication::topLevelAt(global)};
		if (window == nullptr)
		{
			window = scene.window.windowHandle();
		}
		const QPointF local{window->mapFromGlobal(QPointF{global})};
		switch (step.kind)
		{
		case InputKind::MouseMove:
			break;
		case InputKind::MousePress:
		case InputKind::MouseDoubleClick:
			held |= step.button;
			pressWindow = window;
			s_timestamp += step.kind == InputKind::MouseDoubleClick ? 10 : 1000;
			qt_handleMouseEvent(window, local, QPointF{global}, held, step.button, QEvent::MouseButtonPress, step.modifiers, s_timestamp);
			break;
		case InputKind::MouseRelease:
			held &= ~step.button;
			qt_handleMouseEvent(window, local, QPointF{global}, held, step.button, QEvent::MouseButtonRelease, step.modifiers, s_timestamp += 10);
			break;
		case InputKind::Wheel:
			qt_handleWheelEvent(window, local, QPointF{global}, QPoint{}, QPoint{0, 120}, step.modifiers, Qt::NoScrollPhase);
			break;
		case InputKind::KeyPress:
		case InputKind::KeyRelease:
		{
			QWindow* const focused{QGuiApplication::focusWindow() != nullptr ? QGuiApplication::focusWindow() : scene.window.windowHandle()};
			qt_handleKeyEvent(focused, step.kind == InputKind::KeyPress ? QEvent::KeyPress : QEvent::KeyRelease, step.key, step.modifiers, step.text, false, 1);
			break;
		}
		}
		QApplication::processEvents();
	}
}

// The same steps from an agent, through the bridge's input (ui_input at the first step's point,
// each event at its own).
void byAnAgent(Scene& scene, const std::vector<Step>& steps)
{
	std::vector<utility::qt::InputEvent> events;
	for (const Step& step: steps)
	{
		const QPoint global{step.at(scene)};
		const std::uint32_t modifiers{
			(step.modifiers & Qt::ShiftModifier ? 1u : 0u) | (step.modifiers & Qt::ControlModifier ? 2u : 0u) |
			(step.modifiers & Qt::AltModifier ? 4u : 0u) | (step.modifiers & Qt::MetaModifier ? 8u : 0u)};
		events.push_back(utility::qt::InputEvent{
			.kind = step.kind,
			.x = global.x(),
			.y = global.y(),
			.button = step.button == Qt::RightButton ? 2 : step.button == Qt::LeftButton ? 1 : 0,
			.modifiers = modifiers,
			.wheelDelta = step.kind == InputKind::Wheel ? 120 : 0,
			.key = static_cast<std::uint32_t>(step.key),
			.text = step.text.toStdString()});
	}
	const QPoint first{steps.front().at(scene)};
	bool delivered{false};
	const utility::qt::ControlResult result{
		utility::qt::sendInputAt(first.x(), first.y(), events, [&delivered]() { delivered = true; })};
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

// The events `steps` cause, made by `make`, in a fresh scene; `before` sets the scene up (opens
// the menu, moves the mouse) and is not recorded.
std::vector<std::string> eventsOf(
	const std::function<void(Scene&)>& before,
	const std::function<void(Scene&, const std::vector<Step>&)>& make,
	const std::vector<Step>& steps)
{
	QCursor::setPos(QPoint{-500, -500});	 // outside every window, as the gesture starts
	Scene scene;
	before(scene);
	scene.recorder.events.clear();
	make(scene, steps);
	scene.probe->menu().hide();
	return scene.recorder.events;
}

// The same gesture through both paths: the widgets must receive the same events. Returns them,
// for what the scenario expects of a person's input.
std::vector<std::string> requireSame(const std::function<void(Scene&)>& before, const std::vector<Step>& steps)
{
	const std::vector<std::string> physical{eventsOf(before, physically, steps)};
	const std::vector<std::string> agent{eventsOf(before, byAnAgent, steps)};
	INFO("physical: " << std::format("{}", physical));
	INFO("agent:    " << std::format("{}", agent));
	CHECK(agent == physical);
	return physical;
}

// Whether `events` has `event`, and `first` before `then`.
bool has(const std::vector<std::string>& events, const std::string& event)
{
	return std::ranges::find(events, event) != events.end();
}

bool inOrder(const std::vector<std::string>& events, const std::string& first, const std::string& then)
{
	const auto a{std::ranges::find(events, first)};
	const auto b{std::ranges::find(events, then)};
	return a != events.end() && b != events.end() && a < b;
}

QPoint onProbe(Scene& scene)
{
	return scene.probe->mapToGlobal(QPoint{100, 50});
}

QPoint onOther(Scene& scene)
{
	return scene.other->mapToGlobal(QPoint{50, 20});
}

Step mouse(InputKind kind, Qt::MouseButton button, QPoint (*at)(Scene&))
{
	return Step{.kind = kind, .at = at, .button = button};
}

Step key(InputKind kind, int code, Qt::KeyboardModifiers modifiers, QString text, QPoint (*at)(Scene&))
{
	return Step{.kind = kind, .at = at, .key = code, .modifiers = modifiers, .text = std::move(text)};
}

const auto nothing{[](Scene&) {}};
}	 // namespace

TEST_CASE("an agent's input reaches the widgets as a person's does")
{
	const OffscreenApplication application;
	const auto click{[](Qt::MouseButton button, QPoint (*at)(Scene&)) {
		return std::vector<Step>{mouse(InputKind::MousePress, button, at), mouse(InputKind::MouseRelease, button, at)};
	}};

	SECTION("a left click")
	{
		const auto events{requireSame(nothing, click(Qt::LeftButton, onProbe))};
		CHECK(inOrder(events, "probe press left", "probe release left"));
	}

	for (const Qt::ContextMenuTrigger trigger: {Qt::ContextMenuTrigger::Press, Qt::ContextMenuTrigger::Release})
	{
		// Platforms differ: macOS and Linux open the context menu on the press, Windows on the
		// release.
		QGuiApplication::styleHints()->setContextMenuTrigger(trigger);
		DYNAMIC_SECTION("a right click opening a menu, context menu on " << (trigger == Qt::ContextMenuTrigger::Press ? "press" : "release"))
		{
			const auto events{requireSame(nothing, click(Qt::RightButton, onProbe))};
			if (trigger == Qt::ContextMenuTrigger::Press)
			{
				// The menu opens on the press and takes the release.
				CHECK(inOrder(events, "probe context-menu", "probe_menu release right"));
				CHECK_FALSE(has(events, "probe release right"));
			}
			else
			{
				CHECK(inOrder(events, "probe release right", "probe context-menu"));
			}
		}
	}

	SECTION("a left click beside an open menu")
	{
		const auto openMenu{[](Scene& scene) {
			scene.probe->menu().popup(scene.probe->mapToGlobal(QPoint{150, 80}));
			QApplication::processEvents();
		}};
		const auto events{requireSame(openMenu, click(Qt::LeftButton, [](Scene& scene) { return scene.probe->mapToGlobal(QPoint{10, 10}); }))};
		// The open menu takes the press, and closes; the widget under it gets nothing.
		CHECK(has(events, "probe_menu press left"));
		CHECK_FALSE(has(events, "probe press left"));
	}

	SECTION("a double click")
	{
		const auto events{requireSame(
			nothing,
			{mouse(InputKind::MousePress, Qt::LeftButton, onProbe),
			 mouse(InputKind::MouseRelease, Qt::LeftButton, onProbe),
			 mouse(InputKind::MouseDoubleClick, Qt::LeftButton, onProbe),
			 mouse(InputKind::MouseRelease, Qt::LeftButton, onProbe)})};
		CHECK(has(events, "probe double-click left"));
	}

	SECTION("the mouse moving from one widget to another")
	{
		const auto events{requireSame(
			nothing, {mouse(InputKind::MouseMove, Qt::NoButton, onOther), mouse(InputKind::MouseMove, Qt::NoButton, onProbe)})};
		CHECK(inOrder(events, "probe_other enter", "probe_other leave"));
		CHECK(inOrder(events, "probe_other leave", "probe enter"));
	}

	SECTION("the wheel")
	{
		const auto events{requireSame(nothing, {mouse(InputKind::Wheel, Qt::NoButton, onProbe)})};
		// The probe ignores it: it goes on to the window, as a person's does.
		CHECK(inOrder(events, "probe wheel", "probe_window wheel"));
	}

	SECTION("typing goes to the widget with the keyboard focus, wherever the agent aims")
	{
		const auto events{requireSame(
			nothing,
			{key(InputKind::KeyPress, Qt::Key_X, Qt::NoModifier, QStringLiteral("x"), onOther),
			 key(InputKind::KeyRelease, Qt::Key_X, Qt::NoModifier, QStringLiteral("x"), onOther)})};
		CHECK(has(events, "probe_edit key-press X"));
		CHECK_FALSE(has(events, "probe_other key-press X"));
	}

	SECTION("a shortcut triggers its action")
	{
		const auto events{requireSame(
			nothing,
			{key(InputKind::KeyPress, Qt::Key_R, Qt::ControlModifier, QString{}, onProbe),
			 key(InputKind::KeyRelease, Qt::Key_R, Qt::ControlModifier, QString{}, onProbe)})};
		CHECK(has(events, "probe_action triggered"));
	}
}
