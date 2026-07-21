#ifndef MESSAGE_CUSTOM_TRAIL_SHOW_H
#define MESSAGE_CUSTOM_TRAIL_SHOW_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessageCustomTrailShow: public Message<MessageCustomTrailShow>
{
public:
	MessageCustomTrailShow() = default;

	static const std::string getStaticType()
	{
		return "MessageCustomTrailShow";
	}
};

#endif	  // MESSAGE_CUSTOM_TRAIL_SHOW_H
