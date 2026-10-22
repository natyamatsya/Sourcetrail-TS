#include "QtUiControl.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <utility>

#include <QWidget>	// pulls Qt GUI feature macros (QT_CONFIG) before the check below

// captureScreen and the dialog watch need no accessibility support.
#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QEvent>
#include <QLabel>
#include <QList>
#include <QMessageBox>
#include <QMetaObject>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QTimer>

#if QT_CONFIG(accessibility)
#include <QAccessible>
#include <QAccessibleActionInterface>

#include "QtAccessibleRole.h"
#include "QtUiAnchor.h"
#include "QtUiHash.h"
#endif

namespace utility::qt
{
#if QT_CONFIG(accessibility)
namespace
{
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
		const std::string name = child->text(QAccessible::Name).toStdString();
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
			const std::string name = iface->text(QAccessible::Name).toStdString();
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
		actions->doAction(QString::fromStdString(action));
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
	QList<QWidget*> windows;
	for (QWidget* topLevel: QApplication::topLevelWidgets())
	{
		if (topLevel->isVisible() && topLevel->isWindow() && !topLevel->size().isEmpty())
		{
			windows.append(topLevel);
		}
	}
	if (windows.isEmpty())
	{
		return {false, "no visible window to capture", {}, 0, 0};
	}

	const auto area = [](const QWidget* w) { return static_cast<qint64>(w->width()) * w->height(); };
	std::stable_sort(windows.begin(), windows.end(), [&](const QWidget* a, const QWidget* b) {
		const int layerA = compositeLayer(a);
		const int layerB = compositeLayer(b);
		return layerA != layerB ? layerA < layerB : area(a) > area(b);
	});

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
		described += "; cropped to " + std::to_string(wanted.x()) + "," + std::to_string(wanted.y()) + " " +
			std::to_string(wanted.width()) + "x" + std::to_string(wanted.height());
	}
	if (maxWidth != 0 && pixmap.width() > static_cast<int>(maxWidth))
	{
		pixmap = pixmap.scaledToWidth(static_cast<int>(maxWidth), Qt::SmoothTransformation);
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
		described,
		std::vector<std::uint8_t>(bytes.begin(), bytes.end()),
		static_cast<std::uint32_t>(pixmap.width()),
		static_cast<std::uint32_t>(pixmap.height())};
}

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
		const std::lock_guard<std::mutex> lock(s_dialogMutex);
		s_dialog = dialog;
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
				QTimer::singleShot(0, this, [this]() {
					m_pending = false;
					refresh();
				});
			}
		}
		return false;
	}

private:
	bool m_pending = false;
};

QPointer<DialogWatch> s_dialogWatch;	// GUI thread only
}	 // namespace

void watchDialogs(bool on)
{
	if (qApp == nullptr)
	{
		return;
	}
	QMetaObject::invokeMethod(qApp, [on]() {
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
