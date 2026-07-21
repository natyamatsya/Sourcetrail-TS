#ifndef MESSAGE_WINDOW_CLOSED_H
#define MESSAGE_WINDOW_CLOSED_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessageWindowClosed: public Message<MessageWindowClosed>
{
public:
	MessageWindowClosed()
	{
		setSendAsTask(false);
	}

	static const std::string getStaticType()
	{
		return "MessageWindowClosed";
	}
};

#endif	  // MESSAGE_WINDOW_CLOSED_H
