#ifndef MESSAGE_HISTORY_TO_POSITION_H
#define MESSAGE_HISTORY_TO_POSITION_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TabIds.h"
#endif

SRCTRL_EXPORT class MessageHistoryToPosition: public Message<MessageHistoryToPosition>
{
public:
	static const std::string getStaticType()
	{
		return "MessageHistoryToPosition";
	}

	MessageHistoryToPosition(size_t index): index(index)
	{
		setSchedulerId(TabIds::currentTab());
	}

	const size_t index;
};

#endif	  // MESSAGE_HISTORY_TO_POSITION_H
