#ifndef MESSAGE_PING_RECEIVED_H
#define MESSAGE_PING_RECEIVED_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessagePingReceived: public Message<MessagePingReceived>
{
public:
	MessagePingReceived() = default;

	static const std::string getStaticType()
	{
		return "MessagePingReceived";
	}

	std::string ideName;
};

#endif	  // MESSAGE_PING_RECEIVED_H