#ifndef MESSAGE_FOCUSED_SEARCH_VIEW_H
#define MESSAGE_FOCUSED_SEARCH_VIEW_H

#include "Message.h"
#include "TabIds.h"

class MessageFocusedSearchView: public Message<MessageFocusedSearchView>
{
public:
	MessageFocusedSearchView(bool focusIn): focusIn(focusIn)
	{
		setIsLogged(false);
		setSchedulerId(TabIds::currentTab());
	}

	static const std::string getStaticType()
	{
		return "MessageFocusedSearchView";
	}

	const bool focusIn;
};

#endif	  // MESSAGE_FOCUSED_SEARCH_VIEW_H
