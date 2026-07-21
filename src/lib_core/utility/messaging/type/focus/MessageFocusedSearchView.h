#ifndef MESSAGE_FOCUSED_SEARCH_VIEW_H
#define MESSAGE_FOCUSED_SEARCH_VIEW_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TabIds.h"
#endif

SRCTRL_EXPORT class MessageFocusedSearchView: public Message<MessageFocusedSearchView>
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
