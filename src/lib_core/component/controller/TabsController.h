#ifndef TABS_CONTROLLER_H
#define TABS_CONTROLLER_H

#include <deque>
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
#include "TabSession.h"
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

	// Opens the tabs of the project's last session; false when there is none to open.
	bool restoreSession();
	// Keeps the session with the project's bookmarks. Call with m_tabsMutex held.
	void saveSession();

	ViewLayout* m_mainLayout;
	const ViewFactory* m_viewFactory;
	StorageAccess* m_storageAccess;
	ScreenSearchSender* m_screenSearchSender;

	std::map<TabId, std::shared_ptr<Tab>> m_tabs;
	std::map<TabId, std::string> m_tabTitles;	// primary symbol name per tab (for broadcast)
	std::mutex m_tabsMutex;

	bool m_isCreatingTab = false;

	// What the tabs show, kept for the next time the project opens (TabSession.h).
	struct RestoredTab
	{
		std::vector<SearchMatch> shown;	// what it shows again
		std::vector<std::vector<SearchMatch>> before;	// for Back, oldest first
		std::vector<tab_session::Entry> history;	// all of it, as the session keeps it
	};
	tab_session::Recorder m_session;
	std::deque<RestoredTab> m_restoring;	// opened in this order, by addTab
	bool m_sessionPaused = true;	// while the tabs are cleared, and until one opens
	std::tuple<Id, FilePath, size_t> m_scrollToLine;
};

#endif	  // TABS_CONTROLLER_H
