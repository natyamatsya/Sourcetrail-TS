#ifndef MESSAGE_SHOW_STATUS_H
#define MESSAGE_SHOW_STATUS_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessageShowStatus: public Message<MessageShowStatus>
{
public:
	MessageShowStatus() = default;

	static const std::string getStaticType()
	{
		return "MessageShowStatus";
	}
};

#endif	  // MESSAGE_SHOW_STATUS_H
