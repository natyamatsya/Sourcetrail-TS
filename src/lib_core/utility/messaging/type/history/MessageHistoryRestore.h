#ifndef MESSAGE_HISTORY_RESTORE_H
#define MESSAGE_HISTORY_RESTORE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "SearchMatch.h"
#include "TabIds.h"
#endif

// A restored tab's activations before the one it shows (oldest first): Back goes
// through them. Sent to the tab's queue; nothing is shown.
SRCTRL_EXPORT class MessageHistoryRestore: public Message<MessageHistoryRestore>
{
public:
	static const std::string getStaticType()
	{
		return "MessageHistoryRestore";
	}

	MessageHistoryRestore(TabId tabId, std::vector<std::vector<SearchMatch>> entries)
		: entries(std::move(entries))
	{
		setSchedulerId(tabId);
	}

	const std::vector<std::vector<SearchMatch>> entries;
};

#endif	  // MESSAGE_HISTORY_RESTORE_H
