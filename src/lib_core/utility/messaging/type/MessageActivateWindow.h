#ifndef MESSAGE_ACTIVATE_WINDOW_H
#define MESSAGE_ACTIVATE_WINDOW_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessageActivateWindow: public Message<MessageActivateWindow>
{
public:
	MessageActivateWindow() = default;

	static const std::string getStaticType()
	{
		return "MessageActivateWindow";
	}
};

#endif	  // MESSAGE_ACTIVATE_WINDOW_H
