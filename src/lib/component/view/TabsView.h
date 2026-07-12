#ifndef TABS_VIEW_H
#define TABS_VIEW_H

#include <vector>

#include "TabIds.h"
#include "View.h"

struct SearchMatch;

class TabsView: public View
{
public:
	TabsView(ViewLayout* viewLayout);
	~TabsView() override = default;

	std::string getName() const override;

	virtual void clear() = 0;

	virtual void openTab(bool showTab, const SearchMatch& match) = 0;
	virtual void closeTab() = 0;
	virtual void destroyTab(TabId tabId) = 0;
	virtual void selectTab(bool next) = 0;
	virtual void showTab(TabId tabId) = 0;	// activate a specific tab by id
	virtual void updateTab(TabId tabId, const std::vector<SearchMatch>& matches) = 0;
};

#endif	  // TABS_VIEW_H
