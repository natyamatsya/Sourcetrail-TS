#ifndef TABS_CONTROLLER_H
#define TABS_CONTROLLER_H

#include <string>

#include "MessageActivateErrors.h"
#include "MessageIndexingFinished.h"
#include "MessageListener.h"
#include "MessageTabActivate.h"
#include "MessageTabClose.h"
#include "MessageTabOpen.h"
#include "MessageTabOpenWith.h"
#include "MessageTabSelect.h"
#include "MessageTabState.h"
#include "MessageTabsChanged.h"

#include "Controller.h"
#include "Tab.h"
#include "TabsView.h"

struct SearchMatch;

class StorageAccess;
class ViewFactory;
class ViewLayout;

class TabsController
	: public Controller
	, public MessageListener<MessageActivateErrors>
	, public MessageListener<MessageIndexingFinished>
	, public MessageListener<MessageTabActivate>
	, public MessageListener<MessageTabClose>
	, public MessageListener<MessageTabOpen>
	, public MessageListener<MessageTabOpenWith>
	, public MessageListener<MessageTabSelect>
	, public MessageListener<MessageTabState>
{
public:
	TabsController(
		ViewLayout* mainLayout,
		const ViewFactory* viewFactory,
		StorageAccess* storageAccess,
		ScreenSearchSender* screenSearchSender);

	// Controller implementation
	void clear() override;
	
	void addTab(TabId tabId, SearchMatch match);
	void showTab(TabId tabId);
	void removeTab(TabId tabId);
	void destroyTab(TabId tabId);
	void onClearTabs();

private:
	void handleMessage(MessageActivateErrors* message) override;
	void handleMessage(MessageIndexingFinished* message) override;
	void handleMessage(MessageTabClose* message) override;
	void handleMessage(MessageTabOpen* message) override;
	void handleMessage(MessageTabOpenWith* message) override;
	void handleMessage(MessageTabActivate* message) override;
	void handleMessage(MessageTabSelect* message) override;
	void handleMessage(MessageTabState* message) override;

	TabsView* getView() const;

	// Dispatch a MessageTabsChanged snapshot of the current tabs. Call with
	// m_tabsMutex held.
	void broadcastTabs();

	ViewLayout* m_mainLayout;
	const ViewFactory* m_viewFactory;
	StorageAccess* m_storageAccess;
	ScreenSearchSender* m_screenSearchSender;

	std::map<TabId, std::shared_ptr<Tab>> m_tabs;
	std::map<TabId, std::string> m_tabTitles;	// primary symbol name per tab (for broadcast)
	std::mutex m_tabsMutex;

	bool m_isCreatingTab = false;
	std::tuple<Id, FilePath, size_t> m_scrollToLine;
};

#endif	  // TABS_CONTROLLER_H
