#ifndef MESSAGE_LOG_FILTER_CHANGED_H
#define MESSAGE_LOG_FILTER_CHANGED_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "Logger.h"
#endif

SRCTRL_EXPORT class MessageLogFilterChanged: public Message<MessageLogFilterChanged>
{
public:
	MessageLogFilterChanged(const Logger::LogLevelMask filter): logFilter(filter) {}

	static const std::string getStaticType()
	{
		return "MessageLogFilterChanged";
	}

	const Logger::LogLevelMask logFilter;
};

#endif	  // MESSAGE_LOG_FILTER_CHANGED_H
