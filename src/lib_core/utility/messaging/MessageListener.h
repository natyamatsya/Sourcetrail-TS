#ifndef MESSAGE_LISTENER_H
#define MESSAGE_LISTENER_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "MessageBase.h"
#include "MessageListenerBase.h"
#include "MessageQueue.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>
#endif

SRCTRL_EXPORT template <typename MessageType>
class MessageListener: public MessageListenerBase
{
public:
	MessageListener() = default;

private:
	std::string doGetType() const override
	{
		return MessageType::getStaticType();
	}

	void doHandleMessageBase(MessageBase* message) override
	{
		// if (message->isLogged())
		// {
		// 	LOG_INFO_STREAM_BARE(<< "handle " << message->str());
		// }

		handleMessage(dynamic_cast<MessageType*>(message));
	}

	virtual void handleMessage(MessageType* message) = 0;
};

#endif	  // MESSAGE_LISTENER_H
