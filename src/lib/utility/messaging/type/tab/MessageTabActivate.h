#ifndef MESSAGE_TAB_ACTIVATE_H
#define MESSAGE_TAB_ACTIVATE_H

#include "Message.h"
#include "TabIds.h"

// Switch to a specific tab by id (unlike MessageTabSelect, which only cycles
// next/prev). Handled by TabsController -> TabsView::showTab.
class MessageTabActivate: public Message<MessageTabActivate>
{
public:
	MessageTabActivate(TabId tabId): tabId(tabId) {}

	static const std::string getStaticType()
	{
		return "MessageTabActivate";
	}

	const TabId tabId;
};

#endif	  // MESSAGE_TAB_ACTIVATE_H
