#include "QtUiQuery.h"

#include <map>
#include <tuple>
#include <utility>

#include <QWidget>	// pulls Qt GUI feature macros (QT_CONFIG) before the check below

#if defined(SOURCETRAIL_AGENT_CONTROL) && QT_CONFIG(accessibility)
#include <QAccessible>
#include <QAccessibleActionInterface>
#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QRect>
#include <QString>

#include "json-query/JSONQuery"

#include "QtAccessibleRole.h"
#include "agent_snapshot_generated.h"

namespace fb = Sourcetrail::Agent;
#endif

namespace utility::qt
{
#if defined(SOURCETRAIL_AGENT_CONTROL) && QT_CONFIG(accessibility)
namespace
{
using Step = std::tuple<QString, QString, int>;	// role, name, nth-among-siblings

QJsonObject refJson(const std::vector<Step>& path)
{
	QJsonArray steps;
	for (const auto& [role, name, index]: path)
	{
		steps.append(QJsonObject{{"role", role}, {"name", name}, {"index", index}});
	}
	return QJsonObject{{"object_name", QString()}, {"path", steps}};
}

// Accessibility tree -> JSON, matching get_snapshot's shape so the agent writes one
// JSONPath against what it already sees. Each node carries its resolvable `ref`.
QJsonObject buildJsonNode(QAccessibleInterface* iface, std::vector<Step>& path)
{
	QJsonArray children;
	std::map<std::pair<QString, QString>, int> counts;
	const int n = iface->childCount();
	for (int i = 0; i < n; ++i)
	{
		QAccessibleInterface* child = iface->child(i);
		if (child == nullptr)
		{
			continue;
		}
		const QString cr = QString::fromLatin1(accessibleRoleName(child->role()));
		const QString cn = child->text(QAccessible::Name);
		path.push_back({cr, cn, counts[{cr, cn}]++});
		children.append(buildJsonNode(child, path));
		path.pop_back();
	}

	QJsonArray actions;
	if (QAccessibleActionInterface* actionIface = iface->actionInterface())
	{
		for (const QString& action: actionIface->actionNames())
		{
			actions.append(action);
		}
	}
	const QRect r = iface->rect();
	return QJsonObject{
		{"ref", refJson(path)},
		{"role", QString::fromLatin1(accessibleRoleName(iface->role()))},
		{"name", iface->text(QAccessible::Name)},
		{"value", iface->text(QAccessible::Value)},
		{"description", iface->text(QAccessible::Description)},
		{"rect", QJsonObject{{"x", r.x()}, {"y", r.y()}, {"w", r.width()}, {"h", r.height()}}},
		{"actions", actions},
		{"children", children}};
}

// A matched JSON node -> a leaf UiNode FlatBuffer (children omitted).
flatbuffers::Offset<fb::UiNode> jsonToUiNode(flatbuffers::FlatBufferBuilder& b, const QJsonObject& node)
{
	const QJsonObject refObj = node.value("ref").toObject();
	std::vector<flatbuffers::Offset<fb::PathStep>> steps;
	for (const QJsonValue& s: refObj.value("path").toArray())
	{
		const QJsonObject so = s.toObject();
		steps.push_back(fb::CreatePathStep(
			b,
			b.CreateString(so.value("role").toString().toStdString()),
			b.CreateString(so.value("name").toString().toStdString()),
			static_cast<std::uint32_t>(so.value("index").toInt())));
	}
	const auto ref = fb::CreateElementRef(
		b, b.CreateString(refObj.value("object_name").toString().toStdString()), b.CreateVector(steps));

	std::vector<flatbuffers::Offset<flatbuffers::String>> actions;
	for (const QJsonValue& a: node.value("actions").toArray())
	{
		actions.push_back(b.CreateString(a.toString().toStdString()));
	}

	const QJsonObject rc = node.value("rect").toObject();
	const fb::Rect rect(rc.value("x").toInt(), rc.value("y").toInt(), rc.value("w").toInt(), rc.value("h").toInt());

	const auto role = b.CreateString(node.value("role").toString().toStdString());
	const auto name = b.CreateString(node.value("name").toString().toStdString());
	const auto value = b.CreateString(node.value("value").toString().toStdString());
	const auto description = b.CreateString(node.value("description").toString().toStdString());
	const auto actionsVec = b.CreateVector(actions);

	fb::UiNodeBuilder nb(b);
	nb.add_ref(ref);
	nb.add_role(role);
	nb.add_name(name);
	nb.add_value(value);
	nb.add_description(description);
	nb.add_rect(&rect);
	nb.add_actions(actionsVec);
	return nb.Finish();
}
}	 // namespace

QueryResult queryUi(const std::string& jsonPath, std::uint64_t requestId)
{
	QJsonArray roots;
	std::map<std::pair<QString, QString>, int> counts;
	for (QWidget* widget: QApplication::topLevelWidgets())
	{
		QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(widget);
		if (iface == nullptr)
		{
			continue;
		}
		const QString role = QString::fromLatin1(accessibleRoleName(iface->role()));
		const QString name = iface->text(QAccessible::Name);
		std::vector<Step> path{{role, name, counts[{role, name}]++}};
		roots.append(buildJsonNode(iface, path));
	}
	const QJsonObject document{{"roots", roots}};

	const auto compiled = json_query::JSONPath::create(QString::fromStdString(jsonPath));
	if (!compiled)
	{
		return {false, "bad JSONPath: " + std::string(compiled.error().message()), {}};
	}
	const auto matched = compiled->evaluate(QJsonValue(document));
	if (!matched)
	{
		return {false, "JSONPath eval failed: " + std::string(matched.error().message()), {}};
	}

	flatbuffers::FlatBufferBuilder builder;
	std::vector<flatbuffers::Offset<fb::UiNode>> nodes;
	for (const QJsonValue& match: *matched)
	{
		if (match.isObject())
		{
			nodes.push_back(jsonToUiNode(builder, match.toObject()));
		}
	}
	builder.Finish(
		fb::CreateUiSnapshot(builder, requestId, fb::SnapshotFormat_Accessibility, builder.CreateVector(nodes)));
	return {true, "", {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()}};
}
#else
QueryResult queryUi(const std::string&, std::uint64_t)
{
	return {false, "Qt accessibility / agent-control not built", {}};
}
#endif	// SOURCETRAIL_AGENT_CONTROL && QT_CONFIG(accessibility)
}	 // namespace utility::qt
