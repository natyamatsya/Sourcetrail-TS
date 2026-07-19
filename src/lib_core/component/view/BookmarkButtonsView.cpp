#include "BookmarkButtonsView.h"

BookmarkButtonsView::BookmarkButtonsView(ViewLayout* viewLayout): View(viewLayout), m_schedulerId(TabId::NONE)
{
}

std::string BookmarkButtonsView::getName() const
{
	return "BookmarkButtons";
}

TabId BookmarkButtonsView::getSchedulerId() const
{
	return m_schedulerId;
}

void BookmarkButtonsView::setTabId(TabId schedulerId)
{
	m_schedulerId = schedulerId;
}

void BookmarkButtonsView::handleMessage(MessageBookmarkButtonState* message)
{
	setCreateButtonState(message->state);
}
