#ifndef MESSAGE_RESET_ZOOM_H
#define MESSAGE_RESET_ZOOM_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessageResetZoom: public Message<MessageResetZoom>
{
public:
	MessageResetZoom() = default;

	static const std::string getStaticType()
	{
		return "MessageResetZoom";
	}
};

#endif	  // MESSAGE_RESET_ZOOM_H