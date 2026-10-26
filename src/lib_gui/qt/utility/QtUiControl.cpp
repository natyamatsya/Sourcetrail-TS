#include "QtUiControl.h"

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include <QWidget>	// pulls Qt GUI feature macros (QT_CONFIG) before the check below

// captureScreen and the dialog watch need no accessibility support.
#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QContextMenuEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QList>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QWheelEvent>

#if QT_CONFIG(accessibility)
#include <QAccessible>
#include <QAccessibleActionInterface>

#include "MessageCause.h"
#include "QtAccessibleRole.h"
#include "QtUiAnchor.h"
#include "QtUiHash.h"
#endif

namespace utility::qt
{
namespace
{
std::string describeDialog();

// Why `widget` gets no input from an agent, if it does not: a modal window is open
// and `widget` is neither in it nor in a window opened from it (a menu, a combo's
// list). A person's clicks do not reach it either. Delivered anyway, they would act
// under the modal window: closing the dialog whose check asks the question would
// delete it while the check waits for the answer.
std::optional<std::string> blockedByModal(const QWidget* widget)
{
	const QWidget* modal = QApplication::activeModalWidget();
	if (modal == nullptr || widget == nullptr)
	{
		return std::nullopt;
	}
	for (const QWidget* window = widget->window(); window != nullptr;
		 window = window->parentWidget() != nullptr ? window->parentWidget()->window() : nullptr)
	{
		if (window == modal)
		{
			return std::nullopt;
		}
	}
	return "a modal window is in front (" + describeDialog() +
		"): answer it first (a question: machine_send question answer), or act within it";
}
}	 // namespace

#if QT_CONFIG(accessibility)
namespace
{
// The element's own widget, else its nearest ancestor's (a sub-element such as a
// menu item or a cell has none).
QWidget* widgetOf(QAccessibleInterface* iface)
{
	QWidget* widget = qobject_cast<QWidget*>(iface->object());
	for (QAccessibleInterface* p = iface->parent(); widget == nullptr && p != nullptr; p = p->parent())
	{
		widget = qobject_cast<QWidget*>(p->object());
	}
	return widget;
}

// A compact, human-readable rendering of the selector for error messages:
// `#anchor/role[name]/...`, or `/role[name]/...` from a top-level window.
std::string refToString(const std::string& objectName, const std::vector<RefStep>& path)
{
	std::string s = objectName.empty() ? "" : "#" + objectName;
	for (const RefStep& step: path)
	{
		s += "/" + step.role + "[" + step.name + "]";
	}
	return s.empty() ? "/" : s;
}

// The child of `parent` matching one selector step: the `index`-th child sharing
// this (role, name). Mirrors QtUiSnapshot's forward walk exactly, so a ref the
// snapshot emitted resolves back to the same element.
QAccessibleInterface* childMatching(QAccessibleInterface* parent, const RefStep& step)
{
	std::map<std::pair<std::string, std::string>, std::uint32_t> counts;
	const int n = parent->childCount();
	for (int i = 0; i < n; ++i)
	{
		QAccessibleInterface* child = parent->child(i);
		if (child == nullptr)
		{
			continue;
		}
		const std::string role = accessibleRoleName(child->role());
		const std::string name = accessibleElementName(child).toStdString();
		if (role == step.role && name == step.name && counts[{role, name}]++ == step.index)
		{
			return child;
		}
	}
	return nullptr;
}

// Resolve a ref (see QtUiAnchor.h): with an `objectName` anchor, the widget carrying
// it wherever it sits, then `path` down from it; without one, `path` from the
// top-level windows. Returns nullptr with `error` saying why.
QAccessibleInterface* resolve(const std::string& objectName, const std::vector<RefStep>& path, std::string& error)
{
	QAccessibleInterface* current = nullptr;
	std::size_t step = 0;
	if (!objectName.empty())
	{
		QWidget* anchor = widgetNamed(QString::fromStdString(objectName), error);
		if (anchor == nullptr)
		{
			return nullptr;
		}
		current = QAccessible::queryAccessibleInterface(anchor);
		if (current == nullptr)
		{
			error = "widget '" + objectName + "' has no accessible interface";
			return nullptr;
		}
	}
	else
	{
		if (path.empty())
		{
			error = "the ref is empty: pass an object_name, a path, or both";
			return nullptr;
		}
		std::map<std::pair<std::string, std::string>, std::uint32_t> counts;
		for (QWidget* widget: QApplication::topLevelWidgets())
		{
			QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(widget);
			if (iface == nullptr)
			{
				continue;
			}
			const std::string role = accessibleRoleName(iface->role());
			const std::string name = accessibleElementName(iface).toStdString();
			if (role == path[0].role && name == path[0].name && counts[{role, name}]++ == path[0].index)
			{
				current = iface;
				break;
			}
		}
		step = 1;
	}

	for (; current != nullptr && step < path.size(); ++step)
	{
		current = childMatching(current, path[step]);
	}
	if (current == nullptr)
	{
		error = "element not found: " + refToString(objectName, path);
	}
	return current;
}
}	 // namespace

ControlResult invokeAction(
	const std::string& objectName,
	const std::vector<RefStep>& path,
	const std::string& action,
	const std::string& text,
	std::uint64_t expectHash)
{
	std::string error;
	QAccessibleInterface* iface = resolve(objectName, path, error);
	if (iface == nullptr)
	{
		return {false, error};
	}
	if (expectHash != 0 && structuralHash(iface) != expectHash)
	{
		return {false, "element changed since snapshot: " + refToString(objectName, path)};
	}
	if (const std::optional<std::string> blocked = blockedByModal(widgetOf(iface)))
	{
		return {false, *blocked};
	}

	if (!action.empty())
	{
		QAccessibleActionInterface* actions = iface->actionInterface();
		if (actions == nullptr)
		{
			return {false, "element has no actions: " + refToString(objectName, path)};
		}
		if (!actions->actionNames().contains(QString::fromStdString(action)))
		{
			return {false, "action not supported: " + action};
		}
		// A button's accessible Press is an animated click: it releases 100 ms later,
		// and what the button does on release (Back dispatches on it) comes after
		// the ack. Post a plain click instead; the caller acks behind it, so the
		// click has run when the ack arrives (and a modal dialog it opens does not
		// hold the ack: the dialog's own loop delivers it).
		auto* button = qobject_cast<QAbstractButton*>(iface->object());
		if (button != nullptr && QString::fromStdString(action) == QAccessibleActionInterface::pressAction())
		{
			if (!button->isEnabled())
			{
				return {false, "element is disabled: " + refToString(objectName, path)};
			}
			// Under the request's cause: what the click dispatches is on its behalf.
			QMetaObject::invokeMethod(button, withCurrentCause([button]() { button->click(); }), Qt::QueuedConnection);
		}
		else
		{
			actions->doAction(QString::fromStdString(action));
		}
	}

	if (!text.empty())
	{
		QAccessibleEditableTextInterface* editable = iface->editableTextInterface();
		if (editable == nullptr)
		{
			return {false, "element text is not editable: " + refToString(objectName, path)};
		}
		const int len = (iface->textInterface() != nullptr) ? iface->textInterface()->characterCount() : 0;
		editable->replaceText(0, len, QString::fromStdString(text));
	}

	if (action.empty() && text.empty())
	{
		return {false, "invoke_action requires an action or text"};
	}
	return {true, ""};
}

CaptureResult captureElement(
	const std::string& objectName, const std::vector<RefStep>& path, std::uint64_t expectHash)
{
	std::string error;
	QAccessibleInterface* iface = resolve(objectName, path, error);
	if (iface == nullptr)
	{
		return {false, error, {}, 0, 0};
	}
	if (expectHash != 0 && structuralHash(iface) != expectHash)
	{
		return {false, "element changed since snapshot: " + refToString(objectName, path), {}, 0, 0};
	}

	QPixmap pixmap;
	if (QWidget* widget = qobject_cast<QWidget*>(iface->object()))
	{
		pixmap = widget->grab();
	}
	else
	{
		// Sub-element with no QWidget of its own (menu item, cell): grab the nearest
		// ancestor widget cropped to the element's rect (screen -> widget coords).
		QWidget* host = nullptr;
		for (QAccessibleInterface* p = iface->parent(); p != nullptr; p = p->parent())
		{
			if (QWidget* widget = qobject_cast<QWidget*>(p->object()))
			{
				host = widget;
				break;
			}
		}
		if (host == nullptr)
		{
			return {false, "element has no grabbable widget: " + refToString(objectName, path), {}, 0, 0};
		}
		const QRect r = iface->rect();
		pixmap = host->grab(QRect(host->mapFromGlobal(r.topLeft()), r.size()));
	}
	if (pixmap.isNull())
	{
		return {false, "grab produced an empty image", {}, 0, 0};
	}

	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	if (!pixmap.save(&buffer, "PNG"))
	{
		return {false, "PNG encode failed", {}, 0, 0};
	}
	return {
		true,
		"",
		std::vector<std::uint8_t>(bytes.begin(), bytes.end()),
		static_cast<std::uint32_t>(pixmap.width()),
		static_cast<std::uint32_t>(pixmap.height())};
}
#else
ControlResult invokeAction(
	const std::string&, const std::vector<RefStep>&, const std::string&, const std::string&, std::uint64_t)
{
	return {false, "Qt accessibility support not built"};
}

CaptureResult captureElement(const std::string&, const std::vector<RefStep>&, std::uint64_t)
{
	return {false, "Qt accessibility support not built", {}, 0, 0};
}
#endif	// QT_CONFIG(accessibility)

namespace
{
// Paint layer of a top-level window in a screen composite. Qt has no z-order
// query, so stack the way a window manager does: ordinary windows, then dialogs,
// then the active modal, then popups and tooltips.
int compositeLayer(const QWidget* window)
{
	if (window->windowType() == Qt::Popup || window->windowType() == Qt::ToolTip)
	{
		return 3;
	}
	if (window == QApplication::activeModalWidget())
	{
		return 2;
	}
	if (window->windowType() == Qt::Dialog || window->isModal())
	{
		return 1;
	}
	return 0;
}

// Every visible top-level window, in the order a composite paints them: the last is in front.
// Within a layer the larger window goes behind (a wizard over the main window).
QList<QWidget*> windowsBackToFront()
{
	QList<QWidget*> windows;
	for (QWidget* topLevel: QApplication::topLevelWidgets())
	{
		if (topLevel->isVisible() && topLevel->isWindow() && !topLevel->size().isEmpty())
		{
			windows.append(topLevel);
		}
	}

	const auto area = [](const QWidget* w) { return static_cast<qint64>(w->width()) * w->height(); };
	std::stable_sort(windows.begin(), windows.end(), [&](const QWidget* a, const QWidget* b) {
		const int layerA = compositeLayer(a);
		const int layerB = compositeLayer(b);
		return layerA != layerB ? layerA < layerB : area(a) > area(b);
	});
	return windows;
}

// "'title' 800x600 (modal)" for the capture message.
std::string describeWindow(const QWidget* window)
{
	QString title = window->windowTitle();
	if (title.isEmpty())
	{
		title = window->objectName().isEmpty() ? QString::fromLatin1(window->metaObject()->className())
											   : window->objectName();
	}
	QString text = QStringLiteral("'%1' %2x%3").arg(title).arg(window->width()).arg(window->height());
	if (window == QApplication::activeModalWidget())
	{
		text += QStringLiteral(" (modal)");
	}
	return text.toStdString();
}
}	 // namespace

// Every visible top-level window, composited at its global position the way a
// user sees the screen. Grabbing one window would make the result depend on
// focus: a message box would replace the main window, and a dialog behind the
// focus would go unseen.
CaptureResult captureScreen(
	std::uint32_t maxWidth, std::int32_t regionX, std::int32_t regionY, std::int32_t regionW, std::int32_t regionH)
{
	const QList<QWidget*> windows = windowsBackToFront();
	if (windows.isEmpty())
	{
		return {false, "no visible window to capture", {}, 0, 0};
	}

	QRect canvas;
	qreal dpr = 1.0;
	for (const QWidget* window: windows)
	{
		canvas = canvas.united(window->geometry());
		dpr = std::max(dpr, window->devicePixelRatioF());
	}

	QPixmap pixmap(canvas.size() * dpr);
	pixmap.setDevicePixelRatio(dpr);
	pixmap.fill(QColor(0x20, 0x20, 0x20));	  // the desktop behind and between windows
	std::string described;
	{
		QPainter painter(&pixmap);
		for (QWidget* window: windows)
		{
			const QPixmap grabbed = window->grab();
			if (!grabbed.isNull())
			{
				painter.drawPixmap(
					QRect(window->geometry().topLeft() - canvas.topLeft(), window->size()), grabbed);
			}
			described += (described.empty() ? "windows: " : "; ") + describeWindow(window);
		}
	}
	if (pixmap.isNull())
	{
		return {false, "grab produced an empty image", {}, 0, 0};
	}

	// Where the frame sits in global coordinates, for an agent that picks a point
	// out of it: frame pixel p is the global point origin + p / scale.
	QPointF origin = canvas.topLeft();
	double scale = dpr;

	// Crop before the downscale, so a region reads in the pixels the caller saw.
	if (regionW > 0 && regionH > 0)
	{
		const QRect frame(0, 0, pixmap.width(), pixmap.height());
		const QRect wanted = QRect(regionX, regionY, regionW, regionH).intersected(frame);
		if (wanted.isEmpty())
		{
			return {
				false,
				"region " + std::to_string(regionX) + "," + std::to_string(regionY) + " " +
					std::to_string(regionW) + "x" + std::to_string(regionH) + " lies outside the " +
					std::to_string(frame.width()) + "x" + std::to_string(frame.height()) + " frame",
				{},
				0,
				0};
		}
		pixmap = pixmap.copy(wanted);
		origin += QPointF(wanted.topLeft()) / dpr;
		described += "; cropped to " + std::to_string(wanted.x()) + "," + std::to_string(wanted.y()) + " " +
			std::to_string(wanted.width()) + "x" + std::to_string(wanted.height());
	}
	if (maxWidth != 0 && pixmap.width() > static_cast<int>(maxWidth))
	{
		const int fullWidth = pixmap.width();
		pixmap = pixmap.scaledToWidth(static_cast<int>(maxWidth), Qt::SmoothTransformation);
		scale *= static_cast<double>(pixmap.width()) / fullWidth;
	}
	described += QStringLiteral(" | origin %1,%2 scale %3")
					 .arg(qRound(origin.x()))
					 .arg(qRound(origin.y()))
					 .arg(scale, 0, 'g', 6)
					 .toStdString();

	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	if (!pixmap.save(&buffer, "PNG"))
	{
		return {false, "PNG encode failed", {}, 0, 0};
	}
	return {
		true,
		described,
		std::vector<std::uint8_t>(bytes.begin(), bytes.end()),
		static_cast<std::uint32_t>(pixmap.width()),
		static_cast<std::uint32_t>(pixmap.height())};
}

// --- Synthetic input (SendInput) -------------------------------------------------
namespace
{
Qt::MouseButton toQtButton(std::int32_t button)
{
	switch (button)
	{
	case 1:
		return Qt::LeftButton;
	case 2:
		return Qt::RightButton;
	case 3:
		return Qt::MiddleButton;
	default:
		return Qt::NoButton;
	}
}

Qt::MouseButtons toQtButtons(std::uint32_t mask)
{
	Qt::MouseButtons buttons;
	if (mask & 0x1u)
	{
		buttons |= Qt::LeftButton;
	}
	if (mask & 0x2u)
	{
		buttons |= Qt::RightButton;
	}
	if (mask & 0x4u)
	{
		buttons |= Qt::MiddleButton;
	}
	return buttons;
}

Qt::KeyboardModifiers toQtModifiers(std::uint32_t mask)
{
	Qt::KeyboardModifiers modifiers;
	if (mask & 0x1u)
	{
		modifiers |= Qt::ShiftModifier;
	}
	if (mask & 0x2u)
	{
		modifiers |= Qt::ControlModifier;
	}
	if (mask & 0x4u)
	{
		modifiers |= Qt::AltModifier;
	}
	if (mask & 0x8u)
	{
		modifiers |= Qt::MetaModifier;
	}
	return modifiers;
}

// The widget that takes mouse input for `widget`: a scroll area (the graph view,
// the code view) handles it on its viewport.
QWidget* inputReceiver(QWidget* widget)
{
	if (auto* area = qobject_cast<QAbstractScrollArea*>(widget); area != nullptr && area->viewport() != nullptr)
	{
		return area->viewport();
	}
	return widget;
}

// "QGraphicsView viewport of 'graph_view'" for the result message.
std::string describeReceiver(const QWidget* receiver)
{
	// A scroll area's viewport is named for the area it belongs to, and the names
	// Qt gives its own internals (qt_scrollarea_viewport, qt_tabwidget_stackedwidget)
	// say nothing about where the input went.
	const QWidget* shown = receiver;
	if (const auto* area = qobject_cast<const QAbstractScrollArea*>(receiver->parentWidget());
		area != nullptr && area->viewport() == receiver)
	{
		shown = area;
	}
	const QWidget* named = shown;
	while (named != nullptr && (named->objectName().isEmpty() || named->objectName().startsWith(QLatin1String("qt_"))))
	{
		named = named->parentWidget();
	}
	std::string text = shown->metaObject()->className();
	if (shown != receiver)
	{
		text += " viewport";
	}
	if (named != nullptr)
	{
		text += (named == shown ? " '" : " in '") + named->objectName().toStdString() + "'";
	}
	return text;
}

// Delivers `event` to `receiver` on a later turn of the event loop, under the cause
// current now: what the event dispatches is on behalf of the request that sent it.
// Queued calls run in order, and inside a nested event loop, as posted events do; a
// deleted receiver drops it.
void postUnderCause(QWidget* receiver, QEvent* event)
{
	QMetaObject::invokeMethod(
		receiver,
		withCurrentCause([receiver, event = std::shared_ptr<QEvent>(event)]() {
			QCoreApplication::sendEvent(receiver, event.get());
		}),
		Qt::QueuedConnection);
}

// Post the gesture to `receiver`, each event at the global point `toGlobal(event)`
// gives, then `delivered`. Posted rather than sent: a press can open a menu or a
// dialog whose exec() would otherwise block the caller, and the agent's ack with
// it. Posted events and the queued call run in order, so `delivered` follows the
// last event (inside a nested event loop, if one of them opened one). A deleted
// receiver drops its pending events.
void postGesture(
	QWidget* receiver,
	const std::vector<InputEvent>& events,
	const std::function<QPointF(const InputEvent&)>& toGlobal,
	std::function<void()> delivered)
{
	for (const InputEvent& event: events)
	{
		const QPointF global = toGlobal(event);
		const QPointF local = receiver->mapFromGlobal(global);
		const Qt::KeyboardModifiers modifiers = toQtModifiers(event.modifiers);
		const Qt::MouseButton button = toQtButton(event.button);
		switch (event.kind)
		{
		case InputKind::MouseMove:
			postUnderCause(receiver,
				new QMouseEvent(QEvent::MouseMove, local, global, Qt::NoButton, toQtButtons(event.buttons), modifiers));
			break;
		case InputKind::MousePress:
			postUnderCause(
				receiver, new QMouseEvent(QEvent::MouseButtonPress, local, global, button, button, modifiers));
			break;
		case InputKind::MouseRelease:
			postUnderCause(
				receiver, new QMouseEvent(QEvent::MouseButtonRelease, local, global, button, Qt::NoButton, modifiers));
			// A real right-click also makes the window send a context-menu event;
			// posted input does not go through the window, so post it here.
			if (button == Qt::RightButton)
			{
				postUnderCause(receiver,
					new QContextMenuEvent(QContextMenuEvent::Mouse, local.toPoint(), global.toPoint(), modifiers));
			}
			break;
		case InputKind::MouseDoubleClick:
			postUnderCause(
				receiver, new QMouseEvent(QEvent::MouseButtonDblClick, local, global, button, button, modifiers));
			break;
		case InputKind::Wheel:
			postUnderCause(receiver,
				new QWheelEvent(local, global, QPoint(0, 0), QPoint(0, event.wheelDelta), toQtButtons(event.buttons),
					modifiers, Qt::NoScrollPhase, /*inverted*/ false));
			break;
		case InputKind::KeyPress:
			postUnderCause(receiver,
				new QKeyEvent(QEvent::KeyPress, static_cast<int>(event.key), modifiers, QString::fromStdString(event.text)));
			break;
		case InputKind::KeyRelease:
			postUnderCause(receiver,
				new QKeyEvent(QEvent::KeyRelease, static_cast<int>(event.key), modifiers, QString::fromStdString(event.text)));
			break;
		}
	}
	QMetaObject::invokeMethod(qApp, std::move(delivered), Qt::QueuedConnection);
}

// The deepest visible widget under the global point, in the window a frame shows in front
// there (windowsBackToFront): a menu, a modal box, a wizard over the main window. Reaches
// widgets with no accessible element of their own.
QWidget* widgetAt(const QPoint& global)
{
	const QList<QWidget*> windows = windowsBackToFront();
	for (auto it = windows.rbegin(); it != windows.rend(); ++it)
	{
		QWidget* window = *it;
		if (!window->geometry().contains(global))
		{
			continue;
		}
		QWidget* widget = window;
		while (QWidget* child = widget->childAt(widget->mapFromGlobal(global)))
		{
			widget = child;
		}
		return widget;
	}
	return nullptr;
}
}	 // namespace

ControlResult sendInputAt(
	std::int32_t x, std::int32_t y, const std::vector<InputEvent>& events, std::function<void()> delivered)
{
	const QPoint point(x, y);
	QWidget* widget = widgetAt(point);
	if (widget == nullptr)
	{
		return {false, "no window covers the point " + std::to_string(x) + "," + std::to_string(y)};
	}
	if (const std::optional<std::string> blocked = blockedByModal(widget))
	{
		return {false, *blocked};
	}
	QWidget* receiver = inputReceiver(widget);
	postGesture(
		receiver,
		events,
		[point](const InputEvent& event) { return event.centered ? QPointF(point) : QPointF(event.x, event.y); },
		std::move(delivered));
	return {true, "delivered to " + describeReceiver(receiver)};
}

#if QT_CONFIG(accessibility)
ControlResult sendInput(
	const std::string& objectName,
	const std::vector<RefStep>& path,
	const std::vector<InputEvent>& events,
	std::function<void()> delivered)
{
	std::string error;
	QAccessibleInterface* iface = resolve(objectName, path, error);
	if (iface == nullptr)
	{
		return {false, error};
	}
	// Coordinates stay relative to the element, whichever widget receives them.
	QWidget* widget = widgetOf(iface);
	if (widget == nullptr)
	{
		return {false, "element has no widget to receive input: " + refToString(objectName, path)};
	}
	if (const std::optional<std::string> blocked = blockedByModal(widget))
	{
		return {false, *blocked};
	}
	QWidget* receiver = inputReceiver(widget);
	const QRect element = iface->rect();	// global
	postGesture(
		receiver,
		events,
		[element](const InputEvent& event) {
			return event.centered ? QPointF(element.center())
								  : QPointF(element.topLeft()) + QPointF(event.x, event.y);
		},
		std::move(delivered));
	return {true, "delivered to " + describeReceiver(receiver)};
}
#else
ControlResult sendInput(
	const std::string&, const std::vector<RefStep>&, const std::vector<InputEvent>&, std::function<void()>)
{
	return {false, "Qt accessibility support not built: target a point instead"};
}
#endif	// QT_CONFIG(accessibility)

namespace
{
// What is in front of the app; GUI thread only. A modal window first (confirm()
// boxes block the thread that asked, so naming them is what lets an agent see
// why nothing answers), then Sourcetrail's in-window dialogs: elements of a
// QtWindowStack, each of which carries its title in a QLabel named "title".
std::string describeDialog()
{
	if (QWidget* modal = QApplication::activeModalWidget())
	{
		QString text = QStringLiteral("'%1' (modal)").arg(modal->windowTitle());
		if (const auto* box = qobject_cast<QMessageBox*>(modal); box != nullptr && !box->text().isEmpty())
		{
			text += QStringLiteral(": ") + box->text();
		}
		return text.toStdString();
	}
	for (QWidget* widget: QApplication::allWidgets())
	{
		if (widget->inherits("QtWindowStackElement") && widget->isVisible())
		{
			const QLabel* title = widget->findChild<QLabel*>(QStringLiteral("title"));
			return (title != nullptr && !title->text().isEmpty()) ? title->text().toStdString()
																 : widget->metaObject()->className();
		}
	}
	return "";
}

std::mutex s_dialogMutex;
std::string s_dialog;	 // what describeDialog() last returned
std::function<void(const std::string&)> s_dialogChanged;	// GUI thread only

// Recomputes the dialog when a window or a window-stack element shows or hides.
// The recompute is deferred to the next event-loop turn and coalesced, so a burst
// of show/hide events (a dialog and its children) costs one scan.
class DialogWatch: public QObject
{
public:
	using QObject::QObject;

	void refresh()
	{
		const std::string dialog = describeDialog();
		{
			const std::lock_guard<std::mutex> lock(s_dialogMutex);
			if (dialog == s_dialog)
			{
				return;
			}
			s_dialog = dialog;
		}
		if (s_dialogChanged)
		{
			s_dialogChanged(dialog);
		}
	}

protected:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		if (event->type() == QEvent::Show || event->type() == QEvent::Hide)
		{
			if (const auto* widget = qobject_cast<QWidget*>(watched);
				widget != nullptr && (widget->isWindow() || widget->inherits("QtWindowStackElement")) &&
				!m_pending)
			{
				m_pending = true;
				// A dialog a request's work opened or closed is on its behalf.
				QTimer::singleShot(0, this, withCurrentCause([this]() {
					m_pending = false;
					refresh();
				}));
			}
		}
		return false;
	}

private:
	bool m_pending = false;
};

QPointer<DialogWatch> s_dialogWatch;	// GUI thread only
}	 // namespace

void watchDialogs(bool on, std::function<void(const std::string&)> changed)
{
	if (qApp == nullptr)
	{
		return;
	}
	QMetaObject::invokeMethod(qApp, [on, changed = std::move(changed)]() {
		s_dialogChanged = on ? changed : nullptr;
		if (on && s_dialogWatch.isNull())
		{
			s_dialogWatch = new DialogWatch(qApp);
			qApp->installEventFilter(s_dialogWatch);
			s_dialogWatch->refresh();
		}
		else if (!on && !s_dialogWatch.isNull())
		{
			qApp->removeEventFilter(s_dialogWatch);
			delete s_dialogWatch.data();
			const std::lock_guard<std::mutex> lock(s_dialogMutex);
			s_dialog.clear();
		}
	});
}

std::string currentDialog()
{
	const std::lock_guard<std::mutex> lock(s_dialogMutex);
	return s_dialog;
}
}	 // namespace utility::qt
