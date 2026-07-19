#ifndef MESSAGE_HISTORY_UNDO_H
#define MESSAGE_HISTORY_UNDO_H

#include "Message.h"
#include "TabIds.h"

class MessageHistoryUndo: public Message<MessageHistoryUndo>
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
