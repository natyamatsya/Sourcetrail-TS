#include "QtUiControl.h"

#include <algorithm>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

#include <QWidget>	// pulls Qt GUI feature macros (QT_CONFIG) before the check below

// captureScreen and the dialog watch need no accessibility support.
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QComboBox>
#include <QWindow>
#include <qpa/qwindowsysteminterface.h>
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QCursor>
#include <QContextMenuEvent>
#include <QStyleHints>
#include <QEnterEvent>
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
#include <QTreeView>
#include <QWheelEvent>

#if QT_CONFIG(accessibility)
#include <QAccessible>
#include <QAccessibleActionInterface>

#include "MessageCause.h"
#include "QtAccessibleRole.h"
#include "QtUiAnchor.h"
#include "QtUiHash.h"
#include "utilityQtComboBox.h"
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
// The model index of a checkable cell of a list or table view, for toggling its check
// box: Qt's accessible Toggle of a cell changes its selection, not its check state,
// where a person's click on the box checks it. Trees are left alone (their accessible
// rows are flattened, not model rows).
struct CheckableCell
{
	QAbstractItemModel* model;
	QModelIndex index;
};

std::optional<CheckableCell> checkableCell(QAccessibleInterface& iface)
{
	QAccessibleTableCellInterface* cell = iface.tableCellInterface();
	if (cell == nullptr || !iface.state().checkable)
	{
		return std::nullopt;
	}
	QAccessibleInterface* table = cell->table();
	auto* view = table != nullptr ? qobject_cast<QAbstractItemView*>(table->object()) : nullptr;
	if (view == nullptr || view->model() == nullptr || qobject_cast<QTreeView*>(view) != nullptr)
	{
		return std::nullopt;
	}
	const QModelIndex index = view->model()->index(cell->rowIndex(), cell->columnIndex(), view->rootIndex());
	if (!index.isValid() || !(index.flags() & Qt::ItemIsUserCheckable))
	{
		return std::nullopt;
	}
	return CheckableCell {.model = view->model(), .index = index};
}
}	 // namespace

ControlResult invokeAction(
	const std::string& objectName,
	const std::vector<RefStep>& path,
	const std::string& action,
	const std::string& text,
	std::uint64_t expectHash)
{
	keepAccessibleCellsCurrent();
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
		else if (const std::optional<CheckableCell> cell =
					 QString::fromStdString(action) == QAccessibleActionInterface::toggleAction()
						 ? checkableCell(*iface)
						 : std::nullopt)
		{
			const auto& [model, index] = *cell;
			if (!(index.flags() & Qt::ItemIsEnabled))
			{
				return {false, "element is disabled: " + refToString(objectName, path)};
			}
			const bool checked = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
			model->setData(index, checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
		}
		else
		{
			// Posted too, for the same reason: a menu item whose action asks a question
			// (View > Show Title Bars) ran the box's loop right here, and the ack waited
			// for the box to close. The element is looked up again by its id, in case it
			// went meanwhile.
			const QAccessible::Id id = QAccessible::uniqueId(iface);
			QMetaObject::invokeMethod(
				qApp,
				withCurrentCause([id, name = QString::fromStdString(action)]() {
					if (QAccessibleInterface* target = QAccessible::accessibleInterface(id))
					{
						if (QAccessibleActionInterface* targetActions = target->actionInterface())
						{
							targetActions->doAction(name);
						}
					}
				}),
				Qt::QueuedConnection);
		}
	}

	auto* comboBox = qobject_cast<QComboBox*>(iface->object());
	if (!text.empty() && comboBox != nullptr && !comboBox->isEditable())
	{
		// A list to pick from has no text to edit: pick the item, as a person does from its
		// list. `activated` is what the dialogs act on (a colour scheme's preview, ...).
		if (!comboBox->isEnabled())
		{
			return {false, "element is disabled: " + refToString(objectName, path)};
		}
		const int index = comboBox->findText(QString::fromStdString(text));
		if (index < 0)
		{
			return {
				false,
				std::format(
					"no item \"{}\" to pick in {}: it has {}",
					text,
					refToString(objectName, path),
					utility::quotedItems(*comboBox))};
		}
		QMetaObject::invokeMethod(
			comboBox,
			withCurrentCause([comboBox, index]() {
				comboBox->setCurrentIndex(index);
				emit comboBox->activated(index);
				emit comboBox->textActivated(comboBox->itemText(index));
			}),
			Qt::QueuedConnection);
	}
	else if (!text.empty())
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
	std::ranges::stable_sort(windows, [&](const QWidget* a, const QWidget* b) {
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

// The agent's pointer and buttons, as the platform reports a person's: the press's window
// takes the moves and the release while a button is held (the platform's implicit grab), and a
// press soon after one at the same place is reported so that Qt makes it a double click.
struct Pointer
{
	QPointer<QWindow> pressWindow;
	Qt::MouseButtons held;
	QElapsedTimer clock;
	int lastTimestamp = 0;

	int nextTimestamp(int after)
	{
		if (!clock.isValid())
		{
			clock.start();
		}
		lastTimestamp = std::max(lastTimestamp + after, static_cast<int>(clock.elapsed()));
		return lastTimestamp;
	}
};
Pointer s_pointer;

// The window a person's mouse event at `global` goes to: the press's window while a button is
// held, else the window under the point (a popup in front included), else `fallback`'s.
QWindow* windowForMouse(const QPointF& global, const QWidget* fallback)
{
	if (s_pointer.held != Qt::NoButton && s_pointer.pressWindow)
	{
		return s_pointer.pressWindow;
	}
	if (QWindow* window = QGuiApplication::topLevelAt(global.toPoint()))
	{
		return window;
	}
	return fallback->window()->windowHandle();
}

// The pointer moves to `global`, as a person's does before anything happens there: headless
// (offscreen), QCursor::setPos moves the platform's pointer, which reports the move and the
// enter and leave between windows as an OS does; on a screen the person's own pointer stays
// where it is (the agent does not take it), and the move goes to the window as the platform
// reports one; KDDockWidgets follows the agent's pointer from its events then.
void movePointerTo(const QPointF& global, const QWidget* fallback)
{
	if (QGuiApplication::platformName() == QLatin1String("offscreen"))
	{
		if (QCursor::pos() != global.toPoint())
		{
			QCursor::setPos(global.toPoint());
			// The platform queues the move, enter and leave; an OS reports them as the pointer
			// moves, before what happens at the new place.
			QWindowSystemInterface::flushWindowSystemEvents();
		}
		return;
	}
	QWindow* const window{windowForMouse(global, fallback)};
	QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(window,
		static_cast<ulong>(s_pointer.nextTimestamp(1)), window->mapFromGlobal(global), global, s_pointer.held,
		Qt::NoButton, QEvent::MouseMove, QGuiApplication::keyboardModifiers());
}

// One of the agent's events, handed to Qt as a platform plugin hands it a person's
// (QWindowSystemInterface): through the window, with Qt's popup routing, mouse grab,
// context-menu event, double click, keyboard focus, shortcuts, enter and leave.
// QtUiControlInputTestSuite holds it to a person's input.
void deliver(const InputEvent& event, const QPointF& global, const QWidget* fallback)
{
	const Qt::KeyboardModifiers modifiers = toQtModifiers(event.modifiers);
	const Qt::MouseButton button = toQtButton(event.button);
	switch (event.kind)
	{
	case InputKind::MouseMove:
		movePointerTo(global, fallback);
		break;
	case InputKind::MousePress:
	case InputKind::MouseDoubleClick:
	{
		movePointerTo(global, fallback);
		QWindow* const window{windowForMouse(global, fallback)};
		s_pointer.held |= button;
		s_pointer.pressWindow = window;
		// A double click is the second press within the double-click interval of the first.
		const int timestamp{s_pointer.nextTimestamp(event.kind == InputKind::MouseDoubleClick ? 1 : 1000)};
		QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(window,
			static_cast<ulong>(timestamp), window->mapFromGlobal(global), global, s_pointer.held, button,
			QEvent::MouseButtonPress, modifiers);
		break;
	}
	case InputKind::MouseRelease:
	{
		movePointerTo(global, fallback);
		QWindow* const window{windowForMouse(global, fallback)};
		s_pointer.held &= ~button;
		QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(window,
			static_cast<ulong>(s_pointer.nextTimestamp(1)), window->mapFromGlobal(global), global, s_pointer.held,
			button, QEvent::MouseButtonRelease, modifiers);
		break;
	}
	case InputKind::Wheel:
	{
		movePointerTo(global, fallback);
		QWindow* const window{windowForMouse(global, fallback)};
		QWindowSystemInterface::handleWheelEvent(window, static_cast<ulong>(s_pointer.nextTimestamp(1)),
			window->mapFromGlobal(global), global, QPoint{}, QPoint{0, event.wheelDelta}, modifiers, Qt::NoScrollPhase);
		QWindowSystemInterface::flushWindowSystemEvents();	  // queued: delivered here, in turn
		break;
	}
	case InputKind::KeyPress:
	case InputKind::KeyRelease:
	{
		// Keys go to the window with the keyboard focus, and in it to the focused widget, as a
		// person's do; shortcuts first.
		QWindow* const focused{QGuiApplication::focusWindow()};
		QWindow* const window{focused != nullptr ? focused : fallback->window()->windowHandle()};
#if defined(Q_OS_MACOS)
		// The macOS platform offers a key press to the shortcuts before it reports it as a key.
		if (event.kind == InputKind::KeyPress &&
			QWindowSystemInterface::handleShortcutEvent(window, static_cast<ulong>(s_pointer.nextTimestamp(1)),
				static_cast<int>(event.key), modifiers, 0, 0, 0, QString::fromStdString(event.text)))
		{
			break;
		}
#endif
		QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::SynchronousDelivery>(window,
			event.kind == InputKind::KeyPress ? QEvent::KeyPress : QEvent::KeyRelease, static_cast<int>(event.key),
			modifiers, QString::fromStdString(event.text));
		break;
	}
	}
}

// Post the gesture, each event at the global point `toGlobal(event)` gives, then `delivered`.
// Each goes to Qt as the platform hands it a person's (deliver), so it reaches the widgets as a
// person's does. Posted rather than sent: a press can open a menu or a dialog whose exec()
// would otherwise block the caller, and the agent's ack with it. The posted calls run in order,
// so `delivered` follows the last event (inside a nested event loop, if one of them opened one).
// A deleted receiver drops its pending events.
void postGesture(
	QWidget* receiver,
	const std::vector<InputEvent>& events,
	const std::function<QPointF(const InputEvent&)>& toGlobal,
	std::function<void()> delivered)
{
	for (const InputEvent& event: events)
	{
		QMetaObject::invokeMethod(
			receiver,
			withCurrentCause([receiver, event, global = toGlobal(event)]() {
				deliver(event, global, receiver);
				// What the platform reported meanwhile (a window shown under the pointer entered)
				// comes before the next event, as time passes between a person's.
				QWindowSystemInterface::flushWindowSystemEvents();
			}),
			Qt::QueuedConnection);
	}
	QMetaObject::invokeMethod(qApp, std::move(delivered), Qt::QueuedConnection);
}

// "delivered to ..." for the result: the widget `events` aim at, and where mouse input goes
// meanwhile (an open popup, the widget that grabbed the mouse), or keys (the focused widget).
std::string deliveredTo(QWidget* receiver, const std::vector<InputEvent>& events)
{
	const bool mouse{std::ranges::any_of(events, [](const InputEvent& event) {
		return event.kind != InputKind::KeyPress && event.kind != InputKind::KeyRelease;
	})};
	QWidget* const takesMouse{QApplication::activePopupWidget() != nullptr ? QApplication::activePopupWidget()
																			: QWidget::mouseGrabber()};
	QWidget* const focused{QApplication::focusWidget()};
	std::string text{"delivered to " + describeReceiver(receiver)};
	if (mouse && takesMouse != nullptr && takesMouse != receiver)
	{
		text += ", where " + describeReceiver(takesMouse) + " takes the mouse";
	}
	if (!mouse && focused != nullptr && focused != receiver)
	{
		text += ", where " + describeReceiver(focused) + " has the keyboard focus";
	}
	return text;
}

// The deepest visible widget under the global point, in the window a frame shows in front
// there (windowsBackToFront): a menu, a modal box, a wizard over the main window. Reaches
// widgets with no accessible element of their own.
QWidget* widgetAt(const QPoint& global)
{
	for (QWidget* window: windowsBackToFront() | std::views::reverse)
	{
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
	return {true, deliveredTo(receiver, events)};
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
	// A person cannot click what is not shown (a control that shows on hover, a page
	// not current): delivered anyway, the input did nothing and said it was delivered.
	if (!widget->isVisible())
	{
		return {false, "the element is not shown, so a person could not reach it (it may show while the mouse is over its "
					   "row: hover that first): " + refToString(objectName, path)};
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
	return {true, deliveredTo(receiver, events)};
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
			if (title != nullptr && !title->text().isEmpty())
			{
				return title->text().toStdString();
			}
			return !widget->windowTitle().isEmpty() ? widget->windowTitle().toStdString()
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
