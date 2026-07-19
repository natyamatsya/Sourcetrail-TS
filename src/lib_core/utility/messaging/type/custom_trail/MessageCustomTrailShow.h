#ifndef MESSAGE_CUSTOM_TRAIL_SHOW_H
#define MESSAGE_CUSTOM_TRAIL_SHOW_H

#include "Message.h"

class MessageCustomTrailShow: public Message<MessageCustomTrailShow>
{
public:
	MessageCustomTrailShow() = default;

	static const std::string getStaticType()
	{
		return "MessageCustomTrailShow";
	}
};

#endif	  // MESSAGE_CUSTOM_TRAIL_SHOW_H
