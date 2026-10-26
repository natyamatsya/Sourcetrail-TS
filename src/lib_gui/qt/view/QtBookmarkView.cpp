#include "QtBookmarkView.h"
#include "UiPost.h"

#include "QtBookmarkBrowser.h"
#include "QtBookmarkCreator.h"
#include "MessageStatus.h"
#include "QtMainWindow.h"
#include "TabIds.h"
#include "utilityQt.h"

using namespace std;
using namespace utility;

QtBookmarkView::QtBookmarkView(ViewLayout* viewLayout)
	: BookmarkView(viewLayout), m_controllerProxy(this, TabIds::app())
{
	BookmarkStateMachine::instance()->attach(*this);
}

QtBookmarkView::~QtBookmarkView()
{
	BookmarkStateMachine::instance()->detach(*this);
}

void QtBookmarkView::createWidgetWrapper() 
{
}

void QtBookmarkView::refreshView() 
{
}

void QtBookmarkView::displayBookmarkCreator(const std::vector<std::string>& names, const std::vector<BookmarkCategory>& categories, Id nodeId)
{
	::execution::qt::onUi(getMainWindowforMainView(getViewLayout()), [=, this]() 
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

		BookmarkStateMachine::Editor editor;
		editor.nodeId = static_cast<std::uint64_t>(nodeId);
		editor.fields.name = displayName;
		for (const BookmarkCategory& category: categories)
		{
			editor.categories.push_back(category.getName());
		}
		openCreator(
			bookmarkCreator,
			std::move(editor),
			[proxy = &m_controllerProxy, nodeId](const BookmarkFields& fields) {
				proxy->executeAsTaskWithArgs(
					&BookmarkController::createBookmark, fields.name, fields.comment, fields.category, nodeId);
				MessageStatus("Creating Bookmark for active Token").dispatch();
			});
	});
}

void QtBookmarkView::displayBookmarkCreator(std::shared_ptr<Bookmark> bookmark, const std::vector<BookmarkCategory>& categories)
{
	::execution::qt::onUi(getMainWindowforMainView(getViewLayout()), [=, this]() 
	{
		QtBookmarkCreator* bookmarkCreator = new QtBookmarkCreator(&m_controllerProxy, getMainWindowforMainView(getViewLayout()), bookmark->getId());

		bookmarkCreator->setDisplayName(bookmark->getName());
		bookmarkCreator->setComment(bookmark->getComment());
		bookmarkCreator->setBookmarkCategories(categories);
		bookmarkCreator->setCurrentBookmarkCategory(bookmark->getCategory());

		BookmarkStateMachine::Editor editor;
		editor.bookmarkId = static_cast<std::uint64_t>(bookmark->getId());
		editor.fields = {bookmark->getName(), bookmark->getComment(), bookmark->getCategory().getName()};
		for (const BookmarkCategory& category: categories)
		{
			editor.categories.push_back(category.getName());
		}
		openCreator(
			bookmarkCreator,
			std::move(editor),
			[proxy = &m_controllerProxy, id = bookmark->getId()](const BookmarkFields& fields) {
				proxy->executeAsTaskWithArgs(
					&BookmarkController::editBookmark, id, fields.name, fields.comment, fields.category);
			});
	});
}

void QtBookmarkView::openCreator(
	QtBookmarkCreator* creator, BookmarkStateMachine::Editor editor, std::function<void(const BookmarkFields&)> commit)
{
	if (m_creator)
	{
		m_creator->close();	// one editor at a time
	}
	m_creator = creator;
	creator->setCommit(commit);
	creator->setEditorId(BookmarkStateMachine::instance()->opened(std::move(editor), std::move(commit)));

	creator->show();
	creator->raise();
}

void QtBookmarkView::presentBookmarkFields(const BookmarkFields& fields)
{
	::execution::qt::onUi(getMainWindowforMainView(getViewLayout()), [this, fields]() {
		if (m_creator)
		{
			m_creator->showFields(fields);
		}
	});
}

void QtBookmarkView::dismissBookmarkEditor()
{
	::execution::qt::onUi(getMainWindowforMainView(getViewLayout()), [this]() {
		if (m_creator)
		{
			m_creator->close();
		}
	});
}

void QtBookmarkView::displayBookmarkBrowser(const std::vector<std::shared_ptr<Bookmark>>& bookmarks)
{
	::execution::qt::onUi(getMainWindowforMainView(getViewLayout()), [=, this]() 
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
	::execution::qt::onUi(getMainWindowforMainView(getViewLayout()), [=, this]() 
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
