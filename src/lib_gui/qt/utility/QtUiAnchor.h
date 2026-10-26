#ifndef QT_UI_ANCHOR_H
#define QT_UI_ANCHOR_H

#include <map>
#include <string>

#include <QApplication>
#include <QString>
#include <QWidget>
#if QT_CONFIG(accessibility)
#include <QAccessible>
#endif

// Element-ref anchors, shared by the snapshot and the JSONPath query (which emit
// refs) and by QtUiControl (which resolves them), so the three agree.
//
// A ref is `{ object_name, path }`. With an object_name (the anchor) the element is
// the widget carrying that name, wherever it sits, and `path` steps down from it;
// without one, `path` starts at a top-level window. Anchors are what make a ref
// durable: a rooted path breaks when the main window's title changes (it carries
// the project name) or when a popup joins the top-level windows and shifts the
// nth-of-its-kind counting.
//
// Qt does not keep object names unique (every scroll area has a
// "qt_scrollarea_viewport", Sourcetrail reuses "expand_button" per file, and keeps a
// hidden copy of views like the search bar), so a name is an anchor only when
// exactly one *visible* widget carries it; an ambiguous one would resolve to
// whichever widget came first. Hidden copies are ignored: nothing can act on them.
// All of this runs on the GUI thread.
namespace utility::qt
{
//! Keeps the accessible cells of item views current for an agent. Qt drops a view's
//! cached cell interfaces on a model change only while accessibility is active (an
//! assistive technology is attached); without one, the cells a snapshot built
//! outlive a list's clear(): nameless and stateless, acting on nothing. Whatever
//! reads the accessibility tree for an agent calls this first.
inline void keepAccessibleCellsCurrent()
{
#if QT_CONFIG(accessibility)
	if (!QAccessible::isActive())
	{
		QAccessible::setActive(true);
	}
#endif
}

//! How many visible widgets carry each objectName, application-wide.
inline std::map<QString, int> countWidgetNames()
{
	std::map<QString, int> counts;
	for (const QWidget* widget: QApplication::allWidgets())
	{
		if (widget->isVisible() && !widget->objectName().isEmpty())
		{
			++counts[widget->objectName()];
		}
	}
	return counts;
}

//! The anchor for `object`: its objectName when it is a visible widget and the only
//! visible one carrying that name; else empty.
inline QString anchorFor(const QObject* object, const std::map<QString, int>& names)
{
	const auto* widget = qobject_cast<const QWidget*>(object);
	if (widget == nullptr || !widget->isVisible() || widget->objectName().isEmpty())
	{
		return {};
	}
	const auto it = names.find(widget->objectName());
	return (it != names.end() && it->second == 1) ? widget->objectName() : QString();
}

//! The widget named `name`: the only visible one, else the only one at all;
//! nullptr with `error` set when none or several are.
inline QWidget* widgetNamed(const QString& name, std::string& error)
{
	QWidget* visible = nullptr;
	QWidget* any = nullptr;
	int visibleCount = 0;
	int count = 0;
	for (QWidget* widget: QApplication::allWidgets())
	{
		if (widget->objectName() == name)
		{
			any = widget;
			++count;
			if (widget->isVisible())
			{
				visible = widget;
				++visibleCount;
			}
		}
	}
	if (visibleCount == 1)
	{
		return visible;
	}
	if (visibleCount == 0 && count == 1)
	{
		return any;
	}
	const int ambiguous = visibleCount > 1 ? visibleCount : count;
	error = count == 0 ? "no widget is named '" + name.toStdString() + "'"
					   : std::to_string(ambiguous) + (visibleCount > 1 ? " visible" : "") + " widgets are named '" +
						   name.toStdString() + "', so it cannot address one: use the ref ui_snapshot gives";
	return nullptr;
}
}	 // namespace utility::qt

#endif	  // QT_UI_ANCHOR_H
