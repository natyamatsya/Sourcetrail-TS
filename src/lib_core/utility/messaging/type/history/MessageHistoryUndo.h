#ifndef MESSAGE_HISTORY_UNDO_H
#define MESSAGE_HISTORY_UNDO_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TabIds.h"
#endif

SRCTRL_EXPORT class MessageHistoryUndo: public Message<MessageHistoryUndo>
{
public:
	static const std::string getStaticType()
	{
		return "MessageHistoryUndo";
	}

	MessageHistoryUndo()
	{
		setSchedulerId(TabIds::currentTab());
	}
};

#endif	  // MESSAGE_HISTORY_UNDO_H
