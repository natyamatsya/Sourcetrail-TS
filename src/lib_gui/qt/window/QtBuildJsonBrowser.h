#ifndef QT_BUILD_JSON_BROWSER_H
#define QT_BUILD_JSON_BROWSER_H

#include <memory>
#include <vector>

#include <QWidget>

#include "BuildModelSnapshot.h"

class QModelIndex;
class QPlainTextEdit;
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

	QtJsonTreeModel* m_model;
	QTreeView* m_treeView;
	QPlainTextEdit* m_detailsView;
};

#endif	  // QT_BUILD_JSON_BROWSER_H
