// Module build: LOG_* macros stay textual; backend via `import srctrl.logging` below.
#ifdef SRCTRL_MODULE_BUILD
#define SRCTRL_LOGGING_VIA_IMPORT
#endif

#include "QtUiSnapshot.h"

#include <map>
#include <utility>

#include <QApplication>
#include <QFile>
#include <QMetaObject>
#include <QMetaProperty>
#include <QMetaType>
#include <QTimer>
#include <QVariant>
#include <QWidget>

#include "logging.h"

// Imports come AFTER all textual #includes (include-before-import rule).
#ifdef SRCTRL_MODULE_BUILD
import srctrl.logging;
#endif

#if defined(SOURCETRAIL_AGENT_CONTROL)
#include <flatbuffers/flexbuffers.h>

#include "agent_snapshot_generated.h"

#if QT_CONFIG(accessibility)
#include <QAccessible>

#include "QtAccessibleRole.h"	// shared accessibleRoleName (also used by QtUiControl)
#include "QtUiHash.h"			// structuralHash (per-node version stamp)
#endif

namespace fb = Sourcetrail::Agent;
#endif

namespace utility::qt
{
#if defined(SOURCETRAIL_AGENT_CONTROL)
namespace
{
// One resolved selector step (role, name, nth-among-same-role+name-siblings).
struct StepData
{
	std::string role;
	std::string name;
	std::uint32_t index;
};

// The re-resolvable address of the current node: the accumulated path from a root.
flatbuffers::Offset<fb::ElementRef> buildRef(flatbuffers::FlatBufferBuilder& b, const std::vector<StepData>& path)
{
	std::vector<flatbuffers::Offset<fb::PathStep>> steps;
	steps.reserve(path.size());
	for (const StepData& s: path)
	{
		steps.push_back(fb::CreatePathStep(b, b.CreateSharedString(s.role), b.CreateSharedString(s.name), s.index));
	}
	return fb::CreateElementRef(b, 0, b.CreateVector(steps));
}

// Skip large/binary property values — record the type name instead.
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

// The Q_PROPERTY bag as a schemaless FlexBuffer map (ObjectTree route).
flatbuffers::Offset<flatbuffers::Vector<std::uint8_t>> buildProperties(
	flatbuffers::FlatBufferBuilder& b, const QObject* object)
{
	flexbuffers::Builder fbb;
	fbb.Map([&]() {
		const QMetaObject* mo = object->metaObject();
		for (int i = 0; i < mo->propertyCount(); ++i)
		{
			const QMetaProperty property = mo->property(i);
			if (!property.isReadable())
			{
				continue;
			}
			const char* key = property.name();
			if (isHeavyType(property.metaType().id()))
			{
				fbb.String(key, property.typeName());
				continue;
			}
			const QVariant value = property.read(object);
			switch (value.typeId())
			{
			case QMetaType::Bool:
				fbb.Bool(key, value.toBool());
				break;
			case QMetaType::Char:
			case QMetaType::Short:
			case QMetaType::Int:
			case QMetaType::Long:
			case QMetaType::LongLong:
				fbb.Int(key, value.toLongLong());
				break;
			case QMetaType::UShort:
			case QMetaType::UInt:
			case QMetaType::ULong:
			case QMetaType::ULongLong:
				fbb.UInt(key, value.toULongLong());
				break;
			case QMetaType::Float:
			case QMetaType::Double:
				fbb.Double(key, value.toDouble());
				break;
			default:
				fbb.String(key, value.toString().toStdString());
				break;
			}
		}
	});
	fbb.Finish();
	return b.CreateVector(fbb.GetBuffer());
}

// --- Route 2: QObject / QMetaObject walk -----------------------------------
flatbuffers::Offset<fb::UiNode> buildObjectNode(
	flatbuffers::FlatBufferBuilder& b, const QObject* object, std::vector<StepData>& path)
{
	std::vector<flatbuffers::Offset<fb::UiNode>> children;
	std::map<std::pair<std::string, std::string>, std::uint32_t> counts;
	for (const QObject* child: object->children())
	{
		const std::string cr = child->metaObject()->className();
		const std::string cn = child->objectName().toStdString();
		path.push_back({cr, cn, counts[{cr, cn}]++});
		children.push_back(buildObjectNode(b, child, path));
		path.pop_back();
	}
	const auto kids = b.CreateVector(children);
	const auto ref = buildRef(b, path);
	const auto role = b.CreateString(object->metaObject()->className());
	const auto name = b.CreateString(object->objectName().toStdString());
	const auto props = buildProperties(b, object);

	fb::UiNodeBuilder nb(b);
	nb.add_ref(ref);
	nb.add_role(role);
	nb.add_name(name);
	nb.add_properties(props);
	nb.add_children(kids);
	if (const auto* widget = qobject_cast<const QWidget*>(object))
	{
		const QPoint topLeft = widget->mapToGlobal(QPoint(0, 0));
		const fb::Rect rect(topLeft.x(), topLeft.y(), widget->width(), widget->height());
		nb.add_rect(&rect);	// AddStruct copies immediately
		return nb.Finish();
	}
	return nb.Finish();
}

#if QT_CONFIG(accessibility)

std::uint32_t stateBits(const QAccessible::State& s)
{
	std::uint32_t m = 0;
	if (s.focused) m |= 1u << 0;
	if (s.focusable) m |= 1u << 1;
	if (s.checked) m |= 1u << 2;
	if (s.checkable) m |= 1u << 3;
	if (s.disabled) m |= 1u << 4;
	if (s.selected) m |= 1u << 5;
	if (s.selectable) m |= 1u << 6;
	if (s.invisible) m |= 1u << 7;
	if (s.expandable) m |= 1u << 8;
	if (s.expanded) m |= 1u << 9;
	return m;
}

// --- Route 1: accessibility tree (semantic backbone) -----------------------
flatbuffers::Offset<fb::UiNode> buildAccessibleNode(
	flatbuffers::FlatBufferBuilder& b, QAccessibleInterface* iface, std::vector<StepData>& path)
{
	std::vector<flatbuffers::Offset<fb::UiNode>> children;
	std::map<std::pair<std::string, std::string>, std::uint32_t> counts;
	const int n = iface->childCount();
	for (int i = 0; i < n; ++i)
	{
		QAccessibleInterface* child = iface->child(i);
		if (child == nullptr)
		{
			continue;
		}
		const std::string cr = accessibleRoleName(child->role());
		const std::string cn = child->text(QAccessible::Name).toStdString();
		path.push_back({cr, cn, counts[{cr, cn}]++});
		children.push_back(buildAccessibleNode(b, child, path));
		path.pop_back();
	}
	const auto kids = b.CreateVector(children);
	const auto ref = buildRef(b, path);
	const auto role = b.CreateString(accessibleRoleName(iface->role()));
	const auto name = b.CreateString(iface->text(QAccessible::Name).toStdString());
	const auto value = b.CreateString(iface->text(QAccessible::Value).toStdString());
	const auto description = b.CreateString(iface->text(QAccessible::Description).toStdString());

	std::vector<flatbuffers::Offset<flatbuffers::String>> actions;
	if (QAccessibleActionInterface* actionIface = iface->actionInterface())
	{
		for (const QString& action: actionIface->actionNames())
		{
			actions.push_back(b.CreateString(action.toStdString()));
		}
	}
	const auto actionsVec = b.CreateVector(actions);
	const QRect r = iface->rect();
	const fb::Rect rect(r.x(), r.y(), r.width(), r.height());

	fb::UiNodeBuilder nb(b);
	nb.add_ref(ref);
	nb.add_role(role);
	nb.add_role_id(static_cast<std::int32_t>(iface->role()));
	nb.add_name(name);
	nb.add_value(value);
	nb.add_description(description);
	nb.add_rect(&rect);
	nb.add_state(stateBits(iface->state()));
	nb.add_actions(actionsVec);
	nb.add_children(kids);
	nb.add_hash(structuralHash(iface));
	return nb.Finish();
}
#endif	// QT_CONFIG(accessibility)

void writeSnapshot(const QString& path, SnapshotFormat format)
{
	const std::vector<std::uint8_t> buffer = captureUiSnapshot(format);
	if (buffer.empty())
	{
		LOG_ERROR("UI snapshot: empty buffer");
		return;
	}
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly))
	{
		LOG_ERROR("UI snapshot: failed to open \"" + path.toStdString() + "\"");
		return;
	}
	file.write(reinterpret_cast<const char*>(buffer.data()), static_cast<qint64>(buffer.size()));
	LOG_INFO(
		"UI snapshot saved: \"" + path.toStdString() + "\" (" + std::to_string(buffer.size()) +
		" bytes FlatBuffer)");
}
}	 // namespace
#endif	// SOURCETRAIL_AGENT_CONTROL

std::vector<std::uint8_t> captureUiSnapshot(
	[[maybe_unused]] SnapshotFormat format, [[maybe_unused]] std::uint64_t requestId)
{
#if defined(SOURCETRAIL_AGENT_CONTROL)
	flatbuffers::FlatBufferBuilder builder;
	const QWidgetList topLevels = QApplication::topLevelWidgets();
	std::vector<flatbuffers::Offset<fb::UiNode>> roots;
	bool useAccessibility = false;
	std::uint64_t treeHash = 0;	// whole-tree version stamp (accessibility route)

#if QT_CONFIG(accessibility)
	if (format == SnapshotFormat::Accessibility)
	{
		useAccessibility = true;
		treeHash = detail::kFnvOffset;
		std::map<std::pair<std::string, std::string>, std::uint32_t> counts;
		for (QWidget* widget: topLevels)
		{
			if (QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(widget))
			{
				const std::string role = accessibleRoleName(iface->role());
				const std::string name = iface->text(QAccessible::Name).toStdString();
				std::vector<StepData> path{{role, name, counts[{role, name}]++}};
				roots.push_back(buildAccessibleNode(builder, iface, path));
				detail::hashMixU64(treeHash, structuralHash(iface));
			}
		}
	}
#endif
	if (!useAccessibility)
	{
		std::map<std::pair<std::string, std::string>, std::uint32_t> counts;
		for (QWidget* widget: topLevels)
		{
			const std::string role = widget->metaObject()->className();
			const std::string name = widget->objectName().toStdString();
			std::vector<StepData> path{{role, name, counts[{role, name}]++}};
			roots.push_back(buildObjectNode(builder, widget, path));
		}
	}

	const fb::SnapshotFormat fbFormat = useAccessibility ? fb::SnapshotFormat_Accessibility
														 : fb::SnapshotFormat_ObjectTree;
	builder.Finish(
		fb::CreateUiSnapshot(builder, requestId, fbFormat, builder.CreateVector(roots), treeHash));
	return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
#else
	return {};
#endif
}

void scheduleSnapshotAndQuit(const std::string& path, SnapshotFormat format, int delayMs)
{
	const QString qpath = QString::fromStdString(path);
	QTimer::singleShot(delayMs, [qpath, format]() {
#if defined(SOURCETRAIL_AGENT_CONTROL)
		writeSnapshot(qpath, format);
#else
		(void)qpath;
		(void)format;
		LOG_ERROR("UI snapshot requires SOURCETRAIL_AGENT_CONTROL");
#endif
		QApplication::quit();
	});
}
}	 // namespace utility::qt
