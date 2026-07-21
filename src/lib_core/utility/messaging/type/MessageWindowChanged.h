#ifndef MESSAGE_WINDOW_CHANGED_H
#define MESSAGE_WINDOW_CHANGED_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessageWindowChanged: public Message<MessageWindowChanged>
{
public:
	static const std::string getStaticType()
	{
		return "MessageWindowChanged";
	}
};

#endif	  // MESSAGE_WINDOW_CHANGED_H
