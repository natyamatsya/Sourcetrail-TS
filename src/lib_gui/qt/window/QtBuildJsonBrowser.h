#ifndef QT_BUILD_JSON_BROWSER_H
#define QT_BUILD_JSON_BROWSER_H

#include <memory>
#include <vector>

#include <QWidget>

#include "BuildModelSnapshot.h"

class QModelIndex;
class QLabel;
class QPlainTextEdit;
class QLineEdit;
class QProgressBar;
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

	QtJsonTreeModel* m_model;
	QTreeView* m_treeView;
	QLineEdit* m_searchEdit;
	QLabel* m_searchStatusLabel;
	QProgressBar* m_searchProgressBar;
	QPlainTextEdit* m_detailsView;
};

#endif	  // QT_BUILD_JSON_BROWSER_H
