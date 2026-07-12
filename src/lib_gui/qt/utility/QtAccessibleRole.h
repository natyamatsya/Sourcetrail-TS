#ifndef QT_ACCESSIBLE_ROLE_H
#define QT_ACCESSIBLE_ROLE_H

#include <QtGlobal>

#if QT_CONFIG(accessibility)
#include <QAccessible>

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
}	 // namespace utility::qt
#endif	// QT_CONFIG(accessibility)

#endif	  // QT_ACCESSIBLE_ROLE_H
