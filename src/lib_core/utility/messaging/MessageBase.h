#ifndef MESSAGE_BASE_H
#define MESSAGE_BASE_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <ostream>
#include <sstream>

#include "Id.h"
#include "TabIds.h"
#endif

// GM-attached in the module build ([module.unit]/7 -- extern "C++" contents attach to the global
// module): this type appears in the signatures of the classic-forever MessageQueue seam
// (MessageQueue.cpp, stdexec pimpl), and module attachment is part of a class type's mangling,
// so the seam only links if these types mangle identically on both sides. Playbook rule 11.
#ifdef SRCTRL_MODULE_PURVIEW
export extern "C++" {
#endif
class MessageBase
{
public:
	MessageBase()
		: m_id(s_nextId++)
		, m_schedulerId(TabId::NONE)
	{
	}

	virtual ~MessageBase() = default;

	virtual std::string getType() const = 0;
	virtual void dispatch() = 0;

	Id getId() const
	{
		return m_id;
	}

	TabId getSchedulerId() const
	{
		return m_schedulerId;
	}

	void setSchedulerId(TabId schedulerId)
	{
		m_schedulerId = schedulerId;
	}

	bool sendAsTask() const
	{
		return m_sendAsTask;
	}

	void setSendAsTask(bool sendAsTask)
	{
		m_sendAsTask = sendAsTask;
	}

	bool isParallel() const
	{
		return m_isParallel;
	}

	void setIsParallel(bool isParallel)
	{
		m_isParallel = isParallel;
	}

	bool isReplayed() const
	{
		return m_isReplayed;
	}

	void setIsReplayed(bool isReplayed)
	{
		m_isReplayed = isReplayed;
	}

	bool isLast() const
	{
		return m_isLast;
	}

	void setIsLast(bool isLast)
	{
		m_isLast = isLast;
	}

	bool isLogged() const
	{
		return m_isLogged;
	}

	void setIsLogged(bool isLogged)
	{
		m_isLogged = isLogged;
	}

	void setKeepContent(bool keepContent)
	{
		m_keepContent = keepContent;
	}

	bool keepContent() const
	{
		return m_keepContent;
	}

	virtual void print(std::ostream& os) const = 0;

	std::string str() const
	{
		std::stringstream ss;
		ss << getType() << " ";
		print(ss);
		return ss.str();
	}

private:
	static Id s_nextId;

	Id m_id;
	TabId m_schedulerId;

	bool m_isParallel = false;
	bool m_isReplayed = false;

	bool m_sendAsTask = true;
	bool m_keepContent = false;

	bool m_isLast = true;
	bool m_isLogged = true;
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
inline Id MessageBase::s_nextId = 1;
#ifdef SRCTRL_MODULE_PURVIEW
}
#endif

#endif	  // MESSAGE_BASE_H
