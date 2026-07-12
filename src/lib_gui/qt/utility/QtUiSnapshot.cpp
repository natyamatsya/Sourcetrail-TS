#include "QtUiSnapshot.h"

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMetaProperty>
#include <QMetaType>
#include <QTimer>
#include <QWidget>

#if QT_CONFIG(accessibility)
#include <QAccessible>
#endif

#include "logging.h"

namespace utility::qt
{
namespace
{
QJsonObject rectJson(int x, int y, int w, int h)
{
	return QJsonObject{{"x", x}, {"y", y}, {"w", w}, {"h", h}};
}

// Skip properties whose values are large/binary — emit the type name instead of
// dragging a pixmap/icon/palette through JSON.
bool isHeavyType(int typeId)
{
	switch (typeId)
	{
	case QMetaType::QPixmap:
	case QMetaType::QImage:
	case QMetaType::QBitmap:
	case QMetaType::QIcon:
	case QMetaType::QCursor:
	case QMetaType::QBrush:
	case QMetaType::QPalette:
	case QMetaType::QRegion:
		return true;
	default:
		return false;
	}
}

// --- Route 1: QObject / QMetaObject property walk (raw detail) --------------
QJsonObject snapshotObject(const QObject* object)
{
	QJsonObject node;
	const QMetaObject* mo = object->metaObject();
	node["class"] = QString::fromUtf8(mo->className());
	node["objectName"] = object->objectName();

	QJsonObject props;
	for (int i = 0; i < mo->propertyCount(); ++i)
	{
		const QMetaProperty property = mo->property(i);
		if (!property.isReadable())
		{
			continue;
		}
		const QString name = QString::fromUtf8(property.name());
		if (isHeavyType(property.metaType().id()))
		{
			props[name] = QString::fromUtf8(property.typeName());
			continue;
		}
		props[name] = QJsonValue::fromVariant(property.read(object));
	}
	node["properties"] = props;

	if (const auto* widget = qobject_cast<const QWidget*>(object))
	{
		const QPoint topLeft = widget->mapToGlobal(QPoint(0, 0));	// screen coords
		node["screenRect"] = rectJson(topLeft.x(), topLeft.y(), widget->width(), widget->height());
		node["visible"] = widget->isVisible();
		node["focus"] = widget->hasFocus();
	}

	QJsonArray children;
	for (const QObject* child: object->children())
	{
		children.append(snapshotObject(child));
	}
	node["children"] = children;
	return node;
}

#if QT_CONFIG(accessibility)
// --- Route 2: accessibility tree (semantic backbone) -----------------------
const char* roleName(QAccessible::Role role)
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

QJsonObject snapshotAccessible(QAccessibleInterface* iface)
{
	QJsonObject node;
	node["role"] = QString::fromUtf8(roleName(iface->role()));
	node["roleId"] = static_cast<int>(iface->role());
	node["name"] = iface->text(QAccessible::Name);

	const QString value = iface->text(QAccessible::Value);
	if (!value.isEmpty())
	{
		node["value"] = value;
	}
	const QString description = iface->text(QAccessible::Description);
	if (!description.isEmpty())
	{
		node["description"] = description;
	}

	const QRect rect = iface->rect();	// screen coords
	node["rect"] = rectJson(rect.x(), rect.y(), rect.width(), rect.height());

	const QAccessible::State state = iface->state();
	node["state"] = QJsonObject{
		{"focused", static_cast<bool>(state.focused)},
		{"focusable", static_cast<bool>(state.focusable)},
		{"checked", static_cast<bool>(state.checked)},
		{"checkable", static_cast<bool>(state.checkable)},
		{"disabled", static_cast<bool>(state.disabled)},
		{"selected", static_cast<bool>(state.selected)},
		{"selectable", static_cast<bool>(state.selectable)},
		{"invisible", static_cast<bool>(state.invisible)},
		{"expandable", static_cast<bool>(state.expandable)},
		{"expanded", static_cast<bool>(state.expanded)}};

	if (QAccessibleActionInterface* actions = iface->actionInterface())	// invokable
	{
		QJsonArray names;
		for (const QString& action: actions->actionNames())
		{
			names.append(action);
		}
		node["actions"] = names;
	}

	QJsonArray children;
	const int count = iface->childCount();
	for (int i = 0; i < count; ++i)
	{
		if (QAccessibleInterface* child = iface->child(i))
		{
			children.append(snapshotAccessible(child));
		}
	}
	node["children"] = children;
	return node;
}
#endif	// QT_CONFIG(accessibility)

void writeSnapshot(const QString& path, SnapshotFormat format)
{
	const std::string json = captureUiSnapshot(format);
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
	{
		LOG_ERROR("UI snapshot: failed to open \"" + path.toStdString() + "\"");
		return;
	}
	file.write(json.data(), static_cast<qint64>(json.size()));
	LOG_INFO(
		"UI snapshot saved: \"" + path.toStdString() + "\" (" + std::to_string(json.size()) + " bytes)");
}
}	 // namespace

std::string captureUiSnapshot([[maybe_unused]] SnapshotFormat format)
{
	QJsonArray roots;
	const QWidgetList topLevels = QApplication::topLevelWidgets();
	bool useAccessibility = false;

#if QT_CONFIG(accessibility)
	if (format == SnapshotFormat::Accessibility)
	{
		useAccessibility = true;
		for (QWidget* widget: topLevels)
		{
			if (QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(widget))
			{
				roots.append(snapshotAccessible(iface));
			}
		}
	}
#endif
	if (!useAccessibility)
	{
		for (QWidget* widget: topLevels)
		{
			roots.append(snapshotObject(widget));
		}
	}

	QJsonObject doc;
	doc["format"] = useAccessibility ? QStringLiteral("accessibility") : QStringLiteral("objectTree");
	doc["roots"] = roots;
	return QJsonDocument(doc).toJson(QJsonDocument::Compact).toStdString();
}

void scheduleSnapshotAndQuit(const std::string& jsonPath, SnapshotFormat format, int delayMs)
{
	const QString path = QString::fromStdString(jsonPath);
	QTimer::singleShot(delayMs, [path, format]() {
		writeSnapshot(path, format);
		QApplication::quit();
	});
}
}	 // namespace utility::qt
