#ifndef MESSAGE_TAB_STATE_H
#define MESSAGE_TAB_STATE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "SearchMatch.h"
#endif

SRCTRL_EXPORT class MessageTabState: public Message<MessageTabState>
{
public:
	MessageTabState(TabId tabId, const std::vector<SearchMatch>& searchMatches)
		: tabId(tabId), searchMatches(searchMatches)
	{
	}

	static const std::string getStaticType()
	{
		return "MessageTabState";
	}

	const TabId tabId;
	const std::vector<SearchMatch> searchMatches;
};

#endif	  // MESSAGE_TAB_STATE_H
