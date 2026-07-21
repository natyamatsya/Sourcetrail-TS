#ifndef MESSAGE_TAB_CLOSE_H
#define MESSAGE_TAB_CLOSE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessageTabClose: public Message<MessageTabClose>
{
public:
	static const std::string getStaticType()
	{
		return "MessageTabClose";
	}
};

#endif	  // MESSAGE_TAB_CLOSE_H
