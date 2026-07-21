#ifndef MESSAGE_TAB_OPEN_H
#define MESSAGE_TAB_OPEN_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessageTabOpen: public Message<MessageTabOpen>
{
public:
	static const std::string getStaticType()
	{
		return "MessageTabOpen";
	}
};

#endif	  // MESSAGE_TAB_OPEN_H
