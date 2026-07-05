#include "QtBookmarkView.h"
#include "UiPost.h"
#include "QtViewWidgetWrapper.h"

#include "QtBookmarkBrowser.h"
#include "QtBookmarkCreator.h"
#include "QtMainWindow.h"
#include "TabIds.h"
#include "utilityQt.h"

using namespace std;
using namespace utility;

QtBookmarkView::QtBookmarkView(ViewLayout* viewLayout)
	: BookmarkView(viewLayout), m_controllerProxy(this, TabIds::app()) 
{
}

void QtBookmarkView::createWidgetWrapper() 
{
}

void QtBookmarkView::refreshView() 
{
}

void QtBookmarkView::displayBookmarkCreator(const std::vector<std::string>& names, const std::vector<BookmarkCategory>& categories, Id nodeId)
{
	execution::qt::onUi(QtViewWidgetWrapper::getWidgetOfView(this), [=, this]() 
	{
		QtBookmarkCreator* bookmarkCreator = new QtBookmarkCreator(&m_controllerProxy, getMainWindowforMainView(getViewLayout()));

		std::string displayName;

		for (unsigned int i = 0; i < names.size(); i++)
		{
			displayName += names[i];

			if (i < names.size() - 1)
			{
				displayName += "; ";
			}
		}

		bookmarkCreator->setDisplayName(displayName);
		bookmarkCreator->setBookmarkCategories(categories);
		bookmarkCreator->setNodeId(nodeId);

		bookmarkCreator->show();
		bookmarkCreator->raise();
	});
}

void QtBookmarkView::displayBookmarkCreator(std::shared_ptr<Bookmark> bookmark, const std::vector<BookmarkCategory>& categories)
{
	execution::qt::onUi(QtViewWidgetWrapper::getWidgetOfView(this), [=, this]() 
	{
		QtBookmarkCreator* bookmarkCreator = new QtBookmarkCreator(&m_controllerProxy, getMainWindowforMainView(getViewLayout()), bookmark->getId());

		bookmarkCreator->setDisplayName(bookmark->getName());
		bookmarkCreator->setComment(bookmark->getComment());
		bookmarkCreator->setBookmarkCategories(categories);
		bookmarkCreator->setCurrentBookmarkCategory(bookmark->getCategory());

		bookmarkCreator->show();
		bookmarkCreator->raise();
	});
}

void QtBookmarkView::displayBookmarkBrowser(const std::vector<std::shared_ptr<Bookmark>>& bookmarks)
{
	execution::qt::onUi(QtViewWidgetWrapper::getWidgetOfView(this), [=, this]() 
	{
		if (m_bookmarkBrowser == nullptr)
			m_bookmarkBrowser = new QtBookmarkBrowser(&m_controllerProxy, getMainWindowforMainView(getViewLayout()));

		m_bookmarkBrowser->setBookmarks(bookmarks);
		m_bookmarkBrowser->show();
		m_bookmarkBrowser->raise();
	});
}

void QtBookmarkView::undisplayBookmarkBrowser()
{
	execution::qt::onUi(QtViewWidgetWrapper::getWidgetOfView(this), [=, this]() 
	{
		if (m_bookmarkBrowser != nullptr)
			m_bookmarkBrowser->hide();
	});
}

bool QtBookmarkView::bookmarkBrowserIsVisible() const
{
	if (m_bookmarkBrowser != nullptr)
	{
		return m_bookmarkBrowser->isVisible();
	}
	else
	{
		return false;
	}
}
