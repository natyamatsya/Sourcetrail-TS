#ifndef MESSAGE_TAB_ACTIVATE_H
#define MESSAGE_TAB_ACTIVATE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TabIds.h"
#endif

// Switch to a specific tab by id (unlike MessageTabSelect, which only cycles
// next/prev). Handled by TabsController -> TabsView::showTab.
SRCTRL_EXPORT class MessageTabActivate: public Message<MessageTabActivate>
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
