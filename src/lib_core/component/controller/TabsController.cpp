#include "TabsController.h"

#include "Application.h"
#ifndef SRCTRL_MODULE_BUILD
#include "MessageFind.h"
#include "MessageIndexingFinished.h"
#include "MessageScrollToLine.h"
#include "MessageSearch.h"
#include "MessageWindowChanged.h"
#endif
#include "ScreenSearchInterfaces.h"
#include "TabIds.h"
#include "TaskLambda.h"
#include "TaskManager.h"
#include "TaskScheduler.h"

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

	TaskManager::createScheduler(tabId)->startSchedulerLoopThreaded();

	m_tabs.emplace(
		tabId, std::make_shared<Tab>(tabId, m_viewFactory, m_storageAccess, m_screenSearchSender));
	m_tabTitles[tabId] = match.isValid() ? match.name : std::string();

	MessageWindowChanged().dispatch();

	if (match.isValid())
	{
		MessageSearch msg({match}, NodeTypeSet::all());
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
	}
	else
	{
		TabIds::setCurrentTabId(TabId::NONE);
		m_mainLayout->showOriginalViews();
	}

	broadcastTabs();

	Task::dispatch(TabIds::app(), std::make_shared<TaskLambda>([this]() {
					   m_screenSearchSender->clearMatches();
				   }));
}

void TabsController::removeTab(TabId tabId)
{
	// use app task scheduler thread to stop running tasks of tab
	Task::dispatch(TabIds::background(), std::make_shared<TaskLambda>([tabId, this]() {
					   m_screenSearchSender->clearMatches();

					   TaskScheduler* scheduler = TaskManager::getScheduler(tabId).get();
					   scheduler->terminateRunningTasks();
					   scheduler->stopSchedulerLoop();

					   TaskManager::destroyScheduler(tabId);

					   getView()->destroyTab(tabId);
				   }));
}

void TabsController::destroyTab(TabId tabId)
{
	std::lock_guard<std::mutex> lock(m_tabsMutex);

	// destroy the tab on the qt thread to allow view destruction
	m_tabs.erase(tabId);
	m_tabTitles.erase(tabId);
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
		MessageTabOpenWith(SearchMatch::createCommand(COMMAND_ALL)).dispatch();
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
	broadcastTabs();
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
