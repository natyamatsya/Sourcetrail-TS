#ifndef MESSAGE_WINDOW_FOCUS_H
#define MESSAGE_WINDOW_FOCUS_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessageWindowFocus: public Message<MessageWindowFocus>
{
public:
	MessageWindowFocus(bool focusIn): focusIn(focusIn) {}

	static const std::string getStaticType()
	{
		return "MessageWindowFocus";
	}

	const bool focusIn;
};

#endif	  // MESSAGE_WINDOW_FOCUS_H
