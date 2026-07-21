#ifndef MESSAGE_SCROLL_SPEED_CHANGE_H
#define MESSAGE_SCROLL_SPEED_CHANGE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessageScrollSpeedChange: public Message<MessageScrollSpeedChange>
{
public:
	MessageScrollSpeedChange(float scrollSpeed): scrollSpeed(scrollSpeed) {}

	static const std::string getStaticType()
	{
		return "MessageScrollSpeedChange";
	}

	void print(std::ostream& os) const override
	{
		os << scrollSpeed;
	}

	const float scrollSpeed;
};

#endif	  // MESSAGE_SCROLL_SPEED_CHANGE_H
