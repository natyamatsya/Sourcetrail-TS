#include "QtJsonTreeModel.h"

#include <QFile>
#include <QStringList>

namespace
{

QString jsonPointerForChild(const QString& parentPointer, const QString& token)
{
	if (parentPointer.isEmpty() || parentPointer == "/")
		return "/" + token;
	return parentPointer + "/" + token;
}

nlohmann::json normalizeRootJson(const nlohmann::json& input)
{
	const nlohmann::json* current{&input};
	while (current->is_array() && current->size() == 1)
		current = &(*current)[0];

	if (current->is_array())
	{
		bool looksLikeKeyValuePairs{!current->empty()};
		for (const auto& entry : *current)
		{
			if (!entry.is_array() || entry.size() != 2 || !entry[0].is_string())
			{
				looksLikeKeyValuePairs = false;
				break;
			}
		}
		if (looksLikeKeyValuePairs)
		{
			nlohmann::json object = nlohmann::json::object();
			for (const auto& entry : *current)
				object[entry[0].get<std::string>()] = entry[1];
			return object;
		}
	}

	return *current;
}

}

QtJsonTreeModel::QtJsonTreeModel(QObject* parent)
	: QAbstractItemModel(parent)
	, m_root{std::make_unique<Node>()}
{
	m_root->key = QStringLiteral("root");
	m_root->typeName = QStringLiteral("root");
	m_root->preview = QStringLiteral("root");
	m_root->jsonPointer = QStringLiteral("/");
	m_root->childrenLoaded = true;
}

QtJsonTreeModel::~QtJsonTreeModel() = default;

void QtJsonTreeModel::setEntryPoints(const std::vector<BuildJsonEntryPoint>& entryPoints)
{
	beginResetModel();
	m_root = std::make_unique<Node>();
	m_root->key = QStringLiteral("root");
	m_root->typeName = QStringLiteral("root");
	m_root->preview = QStringLiteral("root");
	m_root->jsonPointer = QStringLiteral("/");
	m_root->childrenLoaded = true;
	m_jsonRootCache.clear();

	for (std::size_t index{0}; index < entryPoints.size(); ++index)
	{
		const BuildJsonEntryPoint& entryPoint{entryPoints[index]};
		auto node{std::make_unique<Node>()};
		node->key = QString::fromStdString(entryPoint.kind.empty()
			? ("entry_point_" + std::to_string(index))
			: entryPoint.kind);
		node->typeName = QStringLiteral("entry_point");
		node->preview = QString::fromStdString(entryPoint.path.str());
		node->jsonPointer = jsonPointerForChild(
			m_root->jsonPointer,
			escapeJsonPointerToken(QString::number(static_cast<qlonglong>(index))));
		node->sourceFile = entryPoint.path;
		node->referenceFile = entryPoint.path;
		node->childrenLoaded = false;
		node->parent = m_root.get();
		m_root->children.push_back(std::move(node));
	}

	endResetModel();
}

QModelIndex QtJsonTreeModel::index(int row, int column, const QModelIndex& parent) const
{
	if (row < 0 || column < 0 || column >= columnCount(parent))
		return {};

	const Node* parentNode{nodeFromIndex(parent)};
	if (!parentNode)
		parentNode = rootNode();
	if (static_cast<std::size_t>(row) >= parentNode->children.size())
		return {};

	const Node* childNode{parentNode->children[static_cast<std::size_t>(row)].get()};
	return createIndex(row, column, const_cast<Node*>(childNode));
}

QModelIndex QtJsonTreeModel::parent(const QModelIndex& index) const
{
	if (!index.isValid())
		return {};

	const Node* childNode{nodeFromIndex(index)};
	if (!childNode || !childNode->parent || childNode->parent == rootNode())
		return {};

	const Node* parentNode{childNode->parent};
	const int row{rowOfNode(*parentNode)};
	if (row < 0)
		return {};

	return createIndex(row, static_cast<int>(Column::KEY), const_cast<Node*>(parentNode));
}

int QtJsonTreeModel::rowCount(const QModelIndex& parent) const
{
	if (parent.isValid() && parent.column() != 0)
		return 0;

	const Node* parentNode{nodeFromIndex(parent)};
	if (!parentNode)
		parentNode = rootNode();
	return static_cast<int>(parentNode->children.size());
}

int QtJsonTreeModel::columnCount(const QModelIndex&  /*parent*/) const
{
	return static_cast<int>(Column::COUNT);
}

QVariant QtJsonTreeModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid())
		return {};

	const Node* node{nodeFromIndex(index)};
	if (!node)
		return {};

	if (role == Qt::DisplayRole)
	{
		switch (static_cast<Column>(index.column()))
		{
		case Column::KEY:
			return node->key;
		case Column::TYPE:
			return node->typeName;
		case Column::VALUE:
			return node->preview;
		case Column::COUNT:
			return {};
		}
	}
	if (role == Qt::ToolTipRole)
		return node->preview;
	if (role == JSON_POINTER_ROLE)
		return node->jsonPointer;
	if (role == SOURCE_FILE_ROLE)
		return QString::fromStdString(node->sourceFile.str());
	if (role == REFERENCE_FILE_ROLE)
		return node->referenceFile
			? QString::fromStdString(node->referenceFile->str())
			: QVariant{};
	if (role == IS_REFERENCE_ROLE)
		return node->referenceFile.has_value();

	return {};
}

QVariant QtJsonTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
		return {};

	switch (static_cast<Column>(section))
	{
	case Column::KEY:
		return QStringLiteral("Key");
	case Column::TYPE:
		return QStringLiteral("Type");
	case Column::VALUE:
		return QStringLiteral("Value");
	case Column::COUNT:
		return {};
	}

	return {};
}

bool QtJsonTreeModel::hasChildren(const QModelIndex& parent) const
{
	const Node* node{nodeFromIndex(parent)};
	if (!node)
		node = rootNode();

	if (!node->children.empty())
		return true;
	return nodeCanLoadChildren(*node);
}

bool QtJsonTreeModel::canFetchMore(const QModelIndex& parent) const
{
	const Node* node{nodeFromIndex(parent)};
	if (!node)
		return false;
	if (node->childrenLoaded)
		return false;
	return nodeCanLoadChildren(*node);
}

void QtJsonTreeModel::fetchMore(const QModelIndex& parent)
{
	Node* node{nodeFromIndex(parent)};
	if (!node)
		return;
	if (node->childrenLoaded)
		return;

	auto newChildren{loadChildren(*node)};
	if (newChildren.empty())
		return;

	const int firstRow{static_cast<int>(node->children.size())};
	const int added{static_cast<int>(newChildren.size())};
	beginInsertRows(parent, firstRow, firstRow + added - 1);
	for (auto& child : newChildren)
		node->children.push_back(std::move(child));
	endInsertRows();
}

const QtJsonTreeModel::Node* QtJsonTreeModel::nodeFromIndex(const QModelIndex& index) const
{
	if (!index.isValid())
		return nullptr;
	return static_cast<const Node*>(index.internalPointer());
}

QtJsonTreeModel::Node* QtJsonTreeModel::nodeFromIndex(const QModelIndex& index)
{
	if (!index.isValid())
		return nullptr;
	return static_cast<Node*>(index.internalPointer());
}

QtJsonTreeModel::Node* QtJsonTreeModel::rootNode()
{
	return m_root.get();
}

const QtJsonTreeModel::Node* QtJsonTreeModel::rootNode() const
{
	return m_root.get();
}

int QtJsonTreeModel::rowOfNode(const Node& node) const
{
	if (!node.parent)
		return -1;

	for (std::size_t row{0}; row < node.parent->children.size(); ++row)
		if (node.parent->children[row].get() == &node)
			return static_cast<int>(row);
	return -1;
}

bool QtJsonTreeModel::nodeCanLoadChildren(const Node& node) const
{
	if (node.referenceFile)
		return true;
	if (!node.value)
		return false;
	return node.value->is_object() || node.value->is_array();
}

std::vector<std::unique_ptr<QtJsonTreeModel::Node>> QtJsonTreeModel::loadChildren(Node& node)
{
	node.childrenLoaded = true;
	std::vector<std::unique_ptr<Node>> children{};

	nlohmann::json sourceValue{};
	if (node.referenceFile)
	{
		const auto rootValue{readJsonRootValue(*node.referenceFile)};
		if (!rootValue)
			return children;
		sourceValue = *rootValue;
		node.value = sourceValue;
		node.sourceFile = *node.referenceFile;
		node.typeName = typeNameForValue(sourceValue);
		node.preview = previewForValue(sourceValue);
	}
	else
	{
		if (!node.value)
			return children;
		sourceValue = *node.value;
	}

	appendChildrenForValue(node, sourceValue, children);
	return children;
}

void QtJsonTreeModel::appendChildrenForValue(
	const Node& parentNode,
	const nlohmann::json& value,
	std::vector<std::unique_ptr<Node>>& children)
{
	if (value.is_object())
	{
		for (auto it = value.begin(); it != value.end(); ++it)
		{
			auto child{std::make_unique<Node>()};
			child->key = QString::fromStdString(it.key());
			child->typeName = typeNameForValue(it.value());
			child->preview = previewForValue(it.value());
			child->jsonPointer = jsonPointerForChild(
				parentNode.jsonPointer,
				escapeJsonPointerToken(child->key));
			child->sourceFile = parentNode.sourceFile;
			child->value = it.value();
			child->childrenLoaded = !(it.value().is_array() || it.value().is_object());
			child->parent = const_cast<Node*>(&parentNode);
			if (it.key() == "jsonFile" && it.value().is_string())
			{
				const FilePath referencePath{
					parentNode.sourceFile.getParentDirectory().getConcatenated("/" + it.value().get<std::string>())};
				child->referenceFile = referencePath;
				child->childrenLoaded = false;
			}
			children.push_back(std::move(child));
		}
		return;
	}

	if (!value.is_array())
		return;

	for (std::size_t index{0}; index < value.size(); ++index)
	{
		const auto& element{value[index]};
		auto child{std::make_unique<Node>()};
		child->key = QStringLiteral("[%1]").arg(static_cast<qlonglong>(index));
		child->typeName = typeNameForValue(element);
		child->preview = previewForValue(element);
		child->jsonPointer = jsonPointerForChild(
			parentNode.jsonPointer, QString::number(static_cast<qlonglong>(index)));
		child->sourceFile = parentNode.sourceFile;
		child->value = element;
		child->childrenLoaded = !(element.is_array() || element.is_object());
		child->parent = const_cast<Node*>(&parentNode);
		children.push_back(std::move(child));
	}
}

std::optional<nlohmann::json> QtJsonTreeModel::readJsonRootValue(const FilePath& path)
{
	const auto cacheIt{m_jsonRootCache.find(path.str())};
	if (cacheIt != m_jsonRootCache.end())
		return cacheIt->second;

	QFile file{QString::fromStdString(path.str())};
	if (!file.open(QIODevice::ReadOnly))
		return std::nullopt;

	const QByteArray bytes{file.readAll()};
	if (bytes.isEmpty())
		return std::nullopt;

	try
	{
		const std::string jsonText{
			bytes.constData(), static_cast<std::size_t>(bytes.size())};
		const nlohmann::json parsed{nlohmann::json::parse(jsonText)};
		const nlohmann::json normalized{normalizeRootJson(parsed)};
		m_jsonRootCache.emplace(path.str(), normalized);
		return std::optional<nlohmann::json>{normalized};
	}
	catch (const std::exception&)
	{
		return std::nullopt;
	}
}

QString QtJsonTreeModel::typeNameForValue(const nlohmann::json& value)
{
	if (value.is_null())
		return QStringLiteral("null");
	if (value.is_boolean())
		return QStringLiteral("bool");
	if (value.is_number())
		return QStringLiteral("number");
	if (value.is_string())
		return QStringLiteral("string");
	if (value.is_array())
		return QStringLiteral("array");
	if (value.is_object())
		return QStringLiteral("object");
	return QStringLiteral("unknown");
}

QString QtJsonTreeModel::previewForValue(const nlohmann::json& value)
{
	if (value.is_string())
	{
		QString preview{QString::fromStdString(value.get<std::string>())};
		if (preview.size() > 120)
			preview = preview.left(117) + QStringLiteral("...");
		return QStringLiteral("\"") + preview + QStringLiteral("\"");
	}
	if (value.is_boolean())
		return value.get<bool>() ? QStringLiteral("true") : QStringLiteral("false");
	if (value.is_number())
		return QString::fromStdString(value.dump());
	if (value.is_array())
		return QStringLiteral("[%1]").arg(static_cast<qlonglong>(value.size()));
	if (value.is_object())
		return QStringLiteral("{%1}").arg(static_cast<qlonglong>(value.size()));
	if (value.is_null())
		return QStringLiteral("null");
	return {};
}

QString QtJsonTreeModel::escapeJsonPointerToken(const QString& token)
{
	QString escaped{token};
	escaped.replace(QStringLiteral("~"), QStringLiteral("~0"));
	escaped.replace(QStringLiteral("/"), QStringLiteral("~1"));
	return escaped;
}
