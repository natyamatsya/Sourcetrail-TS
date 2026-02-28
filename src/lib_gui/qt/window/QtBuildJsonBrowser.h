#ifndef QT_BUILD_JSON_BROWSER_H
#define QT_BUILD_JSON_BROWSER_H

#include <memory>
#include <optional>
#include <vector>

#include <QWidget>

#include "BuildModelSnapshot.h"

class QModelIndex;
class QLabel;
class QPlainTextEdit;
class QLineEdit;
class QProgressBar;
class QStandardItemModel;
class QTabWidget;
class QTreeView;
class QtJsonTreeModel;
class SourceGroup;

class QtBuildJsonBrowser : public QWidget
{
public:
	explicit QtBuildJsonBrowser(QWidget* parent = nullptr);

	void setEntryPoints(const std::vector<BuildJsonEntryPoint>& entryPoints);
	void setSourceGroup(const std::shared_ptr<const SourceGroup>& sourceGroup);

private:
	QString formatSelectionDetails(const QModelIndex& index) const;
	QString formatTargetSelectionDetails(const QModelIndex& index) const;
	void populateTargetTree(const std::optional<BuildModelSnapshot>& snapshot);
	void applySearch(const QString& text);
	void fetchAllChildren(const QModelIndex& parent, std::size_t& visitedNodes);
	std::size_t countLoadedNodes(const QModelIndex& parent) const;
	void collectMatches(
		const QModelIndex& parent,
		const QString& query,
		std::vector<QModelIndex>& matches,
		std::size_t& visitedNodes,
		const std::size_t totalNodes);
	bool indexMatchesQuery(const QModelIndex& index, const QString& query) const;
	void expandAncestors(const QModelIndex& index);

	QTabWidget* m_tabWidget;
	QtJsonTreeModel* m_model;
	QTreeView* m_treeView;
	QLineEdit* m_searchEdit;
	QLabel* m_searchStatusLabel;
	QProgressBar* m_searchProgressBar;
	QPlainTextEdit* m_detailsView;
	QTreeView* m_targetTreeView;
	QStandardItemModel* m_targetModel;
	QPlainTextEdit* m_targetDetailsView;
};

#endif	  // QT_BUILD_JSON_BROWSER_H
