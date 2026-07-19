#ifndef MESSAGE_LISTENER_BASE_H
#define MESSAGE_LISTENER_BASE_H

#include <string>

#include "MessageBase.h"
#include "MessageQueue.h"

class MessageListenerBase
{
public:
	MessageListenerBase(): m_id(s_nextId++) 
	{
		MessageQueue::getInstance()->registerListener(this);
	}

	virtual ~MessageListenerBase()
	{
		if (m_alive)
		{
			m_alive = false;
			MessageQueue::getInstance()->unregisterListener(this);
		}
	}

	Id getId() const
	{
		return m_id;
	}

	std::string getType() const
	{
		if (m_alive)
		{
			return doGetType();
		}
		return "";
	}

	void handleMessageBase(MessageBase* message)
	{
		if (m_alive)
		{
			doHandleMessageBase(message);
		}
	}

	void removedListener()
	{
		m_alive = false;
	}

	virtual TabId getSchedulerId() const
	{
		return TabId::NONE;
	}

private:
	virtual std::string doGetType() const = 0;
	virtual void doHandleMessageBase(MessageBase*) = 0;

	static Id s_nextId;

	Id m_id;
	bool m_alive = true;
};

#endif	  // MESSAGE_LISTENER_BASE_H
