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
class QPushButton;
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
	QString currentTargetPath() const;
	QModelIndex findTopLevelTargetIndexByName(const QString& targetName) const;
	bool selectTopLevelTargetForDependency(const QModelIndex& index);
	void populateTargetTree(const std::optional<BuildModelSnapshot>& snapshot);
	void applySearch(const QString& text);
	void applyTargetFilter(const QString& text);
	bool filterTargetRow(const QModelIndex& parent, int row, const QString& query);
	void fetchAllChildren(const QModelIndex& parent, std::size_t& visitedNodes);
	std::size_t countLoadedNodes(const QModelIndex& parent) const;
	void collectMatches(
		const QModelIndex& parent,
		const QString& query,
		std::vector<QModelIndex>& matches,
		std::size_t& visitedNodes,
		const std::size_t totalNodes);
	bool indexMatchesQuery(const QModelIndex& index, const QString& query) const;
	bool targetIndexMatchesQuery(const QModelIndex& index, const QString& query) const;
	void updateTargetActionButtons(const QModelIndex& index);
	void expandAncestors(const QModelIndex& index);

	QTabWidget* m_tabWidget;
	QtJsonTreeModel* m_model;
	QTreeView* m_treeView;
	QLineEdit* m_searchEdit;
	QLabel* m_searchStatusLabel;
	QProgressBar* m_searchProgressBar;
	QPlainTextEdit* m_detailsView;
	QTreeView* m_targetTreeView;
	QLineEdit* m_targetSearchEdit;
	QLabel* m_targetSearchStatusLabel;
	QStandardItemModel* m_targetModel;
	QPlainTextEdit* m_targetDetailsView;
	QPushButton* m_targetCopyDetailsButton;
	QPushButton* m_targetCopyPathButton;
	QPushButton* m_targetOpenPathButton;
	bool m_syncingTargetSelection{false};
};

#endif	  // QT_BUILD_JSON_BROWSER_H
