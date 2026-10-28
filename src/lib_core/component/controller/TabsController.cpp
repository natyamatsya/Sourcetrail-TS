#include "TabsController.h"

#include "Application.h"
#ifndef SRCTRL_MODULE_BUILD
#include "MessageFind.h"
#include "MessageIndexingFinished.h"
#include "MessageScrollToLine.h"
#include "MessageSearch.h"
#include "MessageWindowChanged.h"
#endif
#include "ProjectStateMachine.h"
#include "ScreenSearchInterfaces.h"
#include "MessageHistoryRestore.h"
#include "TabIds.h"
#include "TabQueues.h"
#include "logging.h"

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.messaging;
#endif

TabsController::TabsController(
	ViewLayout* mainLayout,
	const ViewFactory* viewFactory,
	StorageAccess* storageAccess,
	ScreenSearchSender* screenSearchSender)
	: m_mainLayout(mainLayout)
	, m_viewFactory(viewFactory)
	, m_storageAccess(storageAccess)
	, m_screenSearchSender(screenSearchSender)
	 
{
}

void TabsController::clear()
{
	getView()->clear();
	m_isCreatingTab = false;

	while (true)
	{
		{
			std::lock_guard<std::mutex> lock(m_tabsMutex);
			if (m_tabs.empty())
			{
				break;
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

void TabsController::addTab(TabId tabId, SearchMatch match)
{
	std::lock_guard<std::mutex> lock(m_tabsMutex);

	if (const std::shared_ptr<Application> app = Application::getInstance())
	{
		app->startQueue(tabId);	// a tab's own serial queue, beside the others
	}
	else
	{
		execution::startQueue(tabId, nullptr);
	}

	m_tabs.emplace(
		tabId, std::make_shared<Tab>(tabId, m_viewFactory, m_storageAccess, m_screenSearchSender));
	m_tabTitles[tabId] = match.isValid() ? match.name : std::string();
	m_sessionPaused = false;

	MessageWindowChanged().dispatch();

	// A tab of the last session: all it showed, and its history for Back.
	std::vector<SearchMatch> shown {match};
	if (!m_restoring.empty() && match.isValid() && m_restoring.front().shown.front().name == match.name)
	{
		RestoredTab restored = std::move(m_restoring.front());
		m_restoring.pop_front();
		shown = std::move(restored.shown);
		m_session.seed(tabId, std::move(restored.history));
		if (!restored.before.empty())
		{
			MessageHistoryRestore(tabId, std::move(restored.before)).dispatch();
		}
	}

	if (match.isValid())
	{
		MessageSearch msg(shown, NodeTypeSet::all());
		msg.setSchedulerId(tabId);
		msg.dispatch();

		if (match.tokenIds.size() && std::get<0>(m_scrollToLine) == match.tokenIds[0])
		{
			MessageScrollToLine scrollMsg(std::get<1>(m_scrollToLine), std::get<2>(m_scrollToLine));
			scrollMsg.setSchedulerId(tabId);
			scrollMsg.dispatch();
		}
	}
	else
	{
		MessageFind msg;
		msg.setSchedulerId(tabId);
		msg.dispatch();
	}

	m_scrollToLine = std::make_tuple(0, FilePath(), 0);
	m_isCreatingTab = false;
	broadcastTabs();
}

void TabsController::showTab(TabId tabId)
{
	using enum SearchMatch::CommandType;
	std::lock_guard<std::mutex> lock(m_tabsMutex);

	auto it = m_tabs.find(tabId);
	if (it != m_tabs.end())
	{
		TabIds::setCurrentTabId(tabId);
		it->second->setParentLayout(m_mainLayout);
		m_session.activate(tabId);
		saveSession();
	}
	else
	{
		TabIds::setCurrentTabId(TabId::NONE);
		m_mainLayout->showOriginalViews();
	}

	broadcastTabs();

	execution::post(TabIds::app(), [this]() { m_screenSearchSender->clearMatches(); });
}

void TabsController::removeTab(TabId tabId)
{
	// From the background's queue: the tab's own stops, what is queued there dropped.
	execution::post(TabIds::background(), [tabId, this]() {
		m_screenSearchSender->clearMatches();
		execution::stopQueue(tabId);
		getView()->destroyTab(tabId);
	});
}

void TabsController::destroyTab(TabId tabId)
{
	std::lock_guard<std::mutex> lock(m_tabsMutex);

	// destroy the tab on the qt thread to allow view destruction
	m_tabs.erase(tabId);
	m_tabTitles.erase(tabId);
	m_session.remove(tabId);
	saveSession();
	broadcastTabs();

	if (m_tabs.empty() && Application::getInstance()->isProjectLoaded() && !m_isCreatingTab)
	{
		MessageTabOpen().dispatch();
		m_isCreatingTab = true;
	}
}

void TabsController::onClearTabs()
{
	using enum SearchMatch::CommandType;
	{
		// The project goes: its session stays as it was.
		std::lock_guard<std::mutex> lock(m_tabsMutex);
		m_sessionPaused = true;
		m_session.clear();
		m_restoring.clear();
	}
	TabIds::setCurrentTabId(TabId::NONE);
	m_mainLayout->showOriginalViews();
}

TabsView* TabsController::getView() const
{
	return Controller::getView<TabsView>();
}

void TabsController::handleMessage(MessageActivateErrors*  /*message*/)
{
	using enum SearchMatch::CommandType;
	if (m_tabs.empty() && Application::getInstance()->isProjectLoaded())
	{
		MessageTabOpenWith(SearchMatch::createCommand(COMMAND_ERROR)).dispatch();
	}
}

void TabsController::handleMessage(MessageIndexingFinished*  /*message*/)
{
	using enum SearchMatch::CommandType;
	if (m_tabs.empty() && Application::getInstance()->isProjectLoaded())
	{
		if (!restoreSession())
		{
			MessageTabOpenWith(SearchMatch::createCommand(COMMAND_ALL)).dispatch();
		}
	}
	else
	{
		// No tab is coming: a load that waited for this decision is done.
		ProjectStateMachine::instance()->loaded();
	}
}

void TabsController::handleMessage(MessageTabClose*  /*message*/)
{
	getView()->closeTab();
}

void TabsController::handleMessage(MessageTabOpen*  /*message*/)
{
	if (Application::getInstance()->isProjectLoaded())
	{
		getView()->openTab(true, SearchMatch());
		m_isCreatingTab = true;
	}
}

void TabsController::handleMessage(MessageTabOpenWith* message)
{
	if (!Application::getInstance()->isProjectLoaded())
	{
		return;
	}

	SearchMatch match = message->match;
	if (!match.isValid())
	{
		Id tokenId = message->tokenId;
		if (!tokenId && message->locationId)
		{
			std::vector<Id> tokenIds = m_storageAccess->getNodeIdsForLocationIds(
				{message->locationId});
			if (tokenIds.size())
			{
				tokenId = tokenIds[0];
			}
		}

		if (!tokenId && !message->filePath.empty())
		{
			tokenId = m_storageAccess->getNodeIdForFileNode(message->filePath);

			if (message->line)
			{
				m_scrollToLine = std::make_tuple(tokenId, message->filePath, message->line);
			}
		}

		if (tokenId)
		{
			std::vector<SearchMatch> matches = m_storageAccess->getSearchMatchesForTokenIds({tokenId});
			if (matches.size())
			{
				match = matches[0];
			}
		}
	}

	if (match.isValid())
	{
		// The project's first tab: its first activation ends a load (or the
		// indexing of a project that had none).
		bool firstTab = false;
		{
			std::lock_guard<std::mutex> lock(m_tabsMutex);
			firstTab = m_tabs.empty();
		}
		if (firstTab)
		{
			ProjectStateMachine::instance()->tabOpening();
		}
		getView()->openTab(message->showTab, match);
		m_isCreatingTab = true;
	}
}

void TabsController::handleMessage(MessageTabActivate* message)
{
	getView()->showTab(message->tabId);
}

void TabsController::handleMessage(MessageTabSelect* message)
{
	getView()->selectTab(message->next);
}

void TabsController::handleMessage(MessageTabState* message)
{
	getView()->updateTab(message->tabId, message->searchMatches);

	std::lock_guard<std::mutex> lock(m_tabsMutex);
	for (const SearchMatch& match: message->searchMatches)
	{
		if (match.isValid())
		{
			m_tabTitles[message->tabId] = match.name;
			break;
		}
	}
	if (m_tabs.contains(message->tabId) && m_session.record(message->tabId, message->searchMatches))
	{
		saveSession();
	}
	broadcastTabs();
}

bool TabsController::restoreSession()
{
	const std::shared_ptr<Application> app = Application::getInstance();
	if (!app || !app->restoresTabSession())
	{
		return false;
	}
	const std::string json = m_storageAccess->getTabSession();
	if (json.empty())
	{
		return false;
	}
	const std::expected<tab_session::Session, tab_session::SessionError> session = tab_session::fromJson(json);
	if (!session)
	{
		LOG_WARNING("The tabs of the last session are not restored: " + tab_session::toString(session.error()));
		return false;
	}

	// What is gone since is left out: an activation of nothing that is left, a tab
	// with nothing left to show.
	const tab_session::FindSymbols find = [this](const std::vector<NameHierarchy>& names) {
		const std::vector<Id> ids = m_storageAccess->getNodeIdsForNameHierarchies(names);
		return ids.empty() ? std::vector<SearchMatch>() : m_storageAccess->getSearchMatchesForTokenIds(ids);
	};
	std::vector<RestoredTab> tabs;
	std::size_t activeTab = 0;
	for (std::size_t i = 0; i < session->tabs.size(); i++)
	{
		RestoredTab tab;
		for (const tab_session::Entry& entry: session->tabs[i].history)
		{
			std::vector<SearchMatch> matches = tab_session::toMatches(entry, find);
			if (matches.empty())
			{
				continue;
			}
			if (!tab.shown.empty())
			{
				tab.before.push_back(std::move(tab.shown));
			}
			tab.shown = std::move(matches);
			tab.history.push_back(entry);
		}
		if (tab.shown.empty())
		{
			continue;
		}
		if (i <= session->activeTab)
		{
			activeTab = tabs.size();
		}
		tabs.push_back(std::move(tab));
	}
	if (tabs.empty())
	{
		return false;
	}

	ProjectStateMachine::instance()->tabOpening();
	std::vector<SearchMatch> opened;
	{
		std::lock_guard<std::mutex> lock(m_tabsMutex);
		for (RestoredTab& tab: tabs)
		{
			opened.push_back(tab.shown.front());
			m_restoring.push_back(std::move(tab));
		}
	}
	for (std::size_t i = 0; i < opened.size(); i++)
	{
		getView()->openTab(i == activeTab, opened[i]);
	}
	m_isCreatingTab = true;
	LOG_INFO("Restored " + std::to_string(opened.size()) + " tab(s) of the last session");
	return true;
}

void TabsController::saveSession()
{
	// m_tabsMutex is held by the caller.
	if (m_sessionPaused || !m_restoring.empty())
	{
		return;	// the tabs go, or the session's are still opening
	}
	m_storageAccess->setTabSession(tab_session::toJson(m_session.session()));
}

void TabsController::broadcastTabs()
{
	// m_tabsMutex is held by the caller.
	std::vector<MessageTabsChanged::TabInfo> infos;
	infos.reserve(m_tabs.size());
	const TabId active = TabIds::currentTab();
	for (const auto& entry: m_tabs)
	{
		const TabId tabId = entry.first;
		const auto it = m_tabTitles.find(tabId);
		infos.push_back(
			{tabId, it != m_tabTitles.end() ? it->second : std::string(), tabId == active});
	}
	MessageTabsChanged(std::move(infos)).dispatch();
}
