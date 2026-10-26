#ifndef QT_ACCESSIBLE_ROLE_H
#define QT_ACCESSIBLE_ROLE_H

#include <QtGlobal>

#if QT_CONFIG(accessibility)
#include <QAccessible>
#include <QWidget>

namespace utility::qt
{
//! Normalized string role for a QAccessible::Role. Shared by QtUiSnapshot (which
//! records it in each ElementRef path step) and QtUiControl (which recomputes it
//! to resolve a ref back to a live element) — the two MUST agree, so it lives in
//! one place. See context/DESIGN_AGENT_UI_SNAPSHOT.md.
inline const char* accessibleRoleName(QAccessible::Role role)
{
	switch (role)
	{
	case QAccessible::PushButton: return "button";
	case QAccessible::CheckBox: return "checkbox";
	case QAccessible::RadioButton: return "radiobutton";
	case QAccessible::MenuItem: return "menuitem";
	case QAccessible::MenuBar: return "menubar";
	case QAccessible::PopupMenu: return "menu";
	case QAccessible::EditableText: return "textedit";
	case QAccessible::StaticText: return "label";
	case QAccessible::List: return "list";
	case QAccessible::ListItem: return "listitem";
	case QAccessible::Tree: return "tree";
	case QAccessible::TreeItem: return "treeitem";
	case QAccessible::Table: return "table";
	case QAccessible::Cell: return "cell";
	case QAccessible::Row: return "row";
	case QAccessible::Column: return "column";
	case QAccessible::Window: return "window";
	case QAccessible::Dialog: return "dialog";
	case QAccessible::PageTab: return "tab";
	case QAccessible::PageTabList: return "tablist";
	case QAccessible::ScrollBar: return "scrollbar";
	case QAccessible::ComboBox: return "combobox";
	case QAccessible::Grouping: return "group";
	case QAccessible::ToolBar: return "toolbar";
	case QAccessible::Graphic: return "graphic";
	case QAccessible::Splitter: return "splitter";
	case QAccessible::Pane: return "pane";
	case QAccessible::Client: return "client";
	default: return "unknown";
	}
}

//! The element's name as the snapshot records it and a ref matches it: the name its widget
//! was given (setAccessibleName), else what Qt's accessibility reports. Qt's macOS and Linux
//! combo box reports its current text as its name even when it was given one; the label a
//! form names it after is what tells it apart.
inline QString accessibleElementName(QAccessibleInterface* iface)
{
	if (const QWidget* widget = qobject_cast<const QWidget*>(iface->object());
		widget != nullptr && !widget->accessibleName().isEmpty())
	{
		return widget->accessibleName();
	}
	return iface->text(QAccessible::Name);
}
}	 // namespace utility::qt
#endif	// QT_CONFIG(accessibility)

#endif	  // QT_ACCESSIBLE_ROLE_H
