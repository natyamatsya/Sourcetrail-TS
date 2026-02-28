#ifndef QT_JSON_TREE_MODEL_H
#define QT_JSON_TREE_MODEL_H

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include <QAbstractItemModel>
#include <QJsonValue>

#include "BuildModelSnapshot.h"

class QtJsonTreeModel : public QAbstractItemModel
{
public:
	enum class Column
	{
		KEY = 0,
		TYPE,
		VALUE,
		COUNT
	};

	enum Role
	{
		JSON_POINTER_ROLE = Qt::UserRole + 1,
		SOURCE_FILE_ROLE,
		REFERENCE_FILE_ROLE,
		IS_REFERENCE_ROLE
	};

	explicit QtJsonTreeModel(QObject* parent = nullptr);
	~QtJsonTreeModel() override;

	void setEntryPoints(const std::vector<BuildJsonEntryPoint>& entryPoints);

	QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex{}) const override;
	QModelIndex parent(const QModelIndex& index) const override;
	int rowCount(const QModelIndex& parent = QModelIndex{}) const override;
	int columnCount(const QModelIndex& parent = QModelIndex{}) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
	bool hasChildren(const QModelIndex& parent = QModelIndex{}) const override;
	bool canFetchMore(const QModelIndex& parent) const override;
	void fetchMore(const QModelIndex& parent) override;

private:
	struct Node
	{
		QString key;
		QString typeName;
		QString preview;
		QString jsonPointer;
		FilePath sourceFile;
		std::optional<FilePath> referenceFile;
		std::optional<QJsonValue> value;
		bool childrenLoaded{false};
		Node* parent{nullptr};
		std::vector<std::unique_ptr<Node>> children;
	};

	const Node* nodeFromIndex(const QModelIndex& index) const;
	Node* nodeFromIndex(const QModelIndex& index);
	Node* rootNode();
	const Node* rootNode() const;

	int rowOfNode(const Node& node) const;
	bool nodeCanLoadChildren(const Node& node) const;

	std::vector<std::unique_ptr<Node>> loadChildren(Node& node);
	void appendChildrenForValue(
		const Node& parentNode,
		const QJsonValue& value,
		std::vector<std::unique_ptr<Node>>& children);
	std::optional<QJsonValue> readJsonRootValue(const FilePath& path);

	static QString typeNameForValue(const QJsonValue& value);
	static QString previewForValue(const QJsonValue& value);
	static QString escapeJsonPointerToken(const QString& token);

	std::unique_ptr<Node> m_root;
	std::unordered_map<std::string, QJsonValue> m_jsonRootCache;
};

#endif	  // QT_JSON_TREE_MODEL_H
