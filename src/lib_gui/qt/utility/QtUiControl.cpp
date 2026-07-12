#include "QtUiControl.h"

#include <map>
#include <utility>

#include <QWidget>	// pulls Qt GUI feature macros (QT_CONFIG) before the check below

#if QT_CONFIG(accessibility)
#include <QAccessible>
#include <QAccessibleActionInterface>
#include <QApplication>
#include <QString>

#include "QtAccessibleRole.h"
#endif

namespace utility::qt
{
#if QT_CONFIG(accessibility)
namespace
{
// A compact, human-readable rendering of the selector for error messages.
std::string pathToString(const std::vector<RefStep>& path)
{
	std::string s;
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

// Resolve the absolute path from the top-level widgets. `objectName` (the ref's
// anchor) is honored when set, but the snapshot emits an empty anchor today, so
// the common case is a pure root-relative path. See the objectName-hygiene pass
// in DESIGN_AGENT_UI_SNAPSHOT.md.
QAccessibleInterface* resolve(const std::string& objectName, const std::vector<RefStep>& path)
{
	if (path.empty())
	{
		return nullptr;
	}

	std::map<std::pair<std::string, std::string>, std::uint32_t> counts;
	QAccessibleInterface* current = nullptr;
	for (QWidget* widget: QApplication::topLevelWidgets())
	{
		if (!objectName.empty() && widget->objectName().toStdString() != objectName)
		{
			continue;
		}
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

	for (std::size_t k = 1; current != nullptr && k < path.size(); ++k)
	{
		current = childMatching(current, path[k]);
	}
	return current;
}
}	 // namespace

ControlResult invokeAction(
	const std::string& objectName,
	const std::vector<RefStep>& path,
	const std::string& action,
	const std::string& text)
{
	QAccessibleInterface* iface = resolve(objectName, path);
	if (iface == nullptr)
	{
		return {false, "element not found: " + pathToString(path)};
	}

	if (!action.empty())
	{
		QAccessibleActionInterface* actions = iface->actionInterface();
		if (actions == nullptr)
		{
			return {false, "element has no actions: " + pathToString(path)};
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
			return {false, "element text is not editable: " + pathToString(path)};
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
#else
ControlResult invokeAction(
	const std::string&, const std::vector<RefStep>&, const std::string&, const std::string&)
{
	return {false, "Qt accessibility support not built"};
}
#endif	// QT_CONFIG(accessibility)
}	 // namespace utility::qt
