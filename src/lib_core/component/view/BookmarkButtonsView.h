#ifndef BOOKMARK_BUTTONS_VIEW_H
#define BOOKMARK_BUTTONS_VIEW_H

#include "MessageBookmarkButtonState.h"
#include "MessageListener.h"
#include "View.h"

class BookmarkButtonsView
	: public View
	, public MessageListener<MessageBookmarkButtonState>
{
public:
	BookmarkButtonsView(ViewLayout* viewLayout);
	~BookmarkButtonsView() override = default;

	std::string getName() const override;
	
	TabId getSchedulerId() const override;
	void setTabId(TabId schedulerId);

	virtual void setCreateButtonState(const MessageBookmarkButtonState::ButtonState& state) = 0;

private:
	void handleMessage(MessageBookmarkButtonState* message) override;

	TabId m_schedulerId;
};

#endif	  // BOOKMARK_BUTTONS_VIEW_H
