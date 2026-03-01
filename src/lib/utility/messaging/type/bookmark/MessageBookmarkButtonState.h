#ifndef MESSAGE_BOOKMARK_BUTTON_STATE_H
#define MESSAGE_BOOKMARK_BUTTON_STATE_H

#include "Message.h"

class MessageBookmarkButtonState: public Message<MessageBookmarkButtonState>
{
public:
	enum class ButtonState
	{
		CAN_CREATE = 0,
		CANNOT_CREATE,
		ALREADY_CREATED
	};

	static const std::string getStaticType()
	{
		return "MessageBookmarkButtonState";
	}

	MessageBookmarkButtonState(TabId schedulerId, ButtonState state): state(state)
	{
		setSchedulerId(schedulerId);
	}

	const ButtonState state;
};

#endif	  // MESSAGE_BOOKMARK_BUTTON_STATE_H
