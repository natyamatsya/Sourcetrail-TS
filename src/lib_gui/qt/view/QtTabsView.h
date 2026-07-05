#ifndef QT_TABS_VIEW_H
#define QT_TABS_VIEW_H

#include <QObject>

#include "TabsController.h"
#include "TabsView.h"

class QtTabBar;

class QtTabsView
	: public QObject
	, public TabsView
{
	Q_OBJECT

public:
	QtTabsView(ViewLayout* viewLayout);
	~QtTabsView() override = default;

	// View implementation
	void createWidgetWrapper() override;
	void refreshView() override;

	// TabsView implementation
	void clear() override;
	void openTab(bool showTab, const SearchMatch& match) override;
	void closeTab() override;
	void destroyTab(TabId tabId) override;
	void selectTab(bool next) override;
	void updateTab(TabId tabId, const std::vector<SearchMatch>& matches) override;

private slots:
	void addTab();
	void insertTab(bool showTab, const SearchMatch& match);
	void changedTab(int index);
	void removeTab(int index);
	void closeTabsToRight(int index);

private:
	void setTabState(int idx, const std::vector<SearchMatch>& matches);

	void setStyleSheet();


	QWidget* m_widget = nullptr;
	QtTabBar* m_tabBar;

	size_t m_insertedTabCount = 0;
};

#endif	  // QT_TABS_VIEW_H
