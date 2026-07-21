#ifndef MESSAGE_HISTORY_REDO_H
#define MESSAGE_HISTORY_REDO_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TabIds.h"
#endif

SRCTRL_EXPORT class MessageHistoryRedo: public Message<MessageHistoryRedo>
{
public:
	static const std::string getStaticType()
	{
		return "MessageHistoryRedo";
	}

	MessageHistoryRedo()
	{
		setSchedulerId(TabIds::currentTab());
	}
};

#endif	  // MESSAGE_HISTORY_REDO_H
