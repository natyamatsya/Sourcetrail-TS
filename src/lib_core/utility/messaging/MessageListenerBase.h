#ifndef MESSAGE_LISTENER_BASE_H
#define MESSAGE_LISTENER_BASE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "MessageBase.h"
#include "MessageQueue.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>
#endif

// GM-attached in the module build ([module.unit]/7 -- extern "C++" contents attach to the global
// module): this type appears in the signatures of the classic-forever MessageQueue seam
// (MessageQueue.cpp, stdexec pimpl), and module attachment is part of a class type's mangling,
// so the seam only links if these types mangle identically on both sides. Playbook rule 11.
#ifdef SRCTRL_MODULE_PURVIEW
export extern "C++" {
#endif
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
#ifdef SRCTRL_MODULE_PURVIEW
}
#endif

// Outside the export block (an out-of-class static member definition cannot itself be
// exported), but still extern "C++": clang attaches a naked purview definition to the
// module, which conflicts with the global-module in-class declaration.
#ifdef SRCTRL_MODULE_PURVIEW
extern "C++" {
#endif
inline Id MessageListenerBase::s_nextId = 1;
#ifdef SRCTRL_MODULE_PURVIEW
}
#endif

#endif	  // MESSAGE_LISTENER_BASE_H
