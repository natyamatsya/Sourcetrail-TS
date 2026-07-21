#ifndef MESSAGE_STATUS_FILTER_CHANGED_H
#define MESSAGE_STATUS_FILTER_CHANGED_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "Status.h"
#endif

SRCTRL_EXPORT class MessageStatusFilterChanged: public Message<MessageStatusFilterChanged>
{
public:
	MessageStatusFilterChanged(const StatusFilter filter): statusFilter(filter) {}

	static const std::string getStaticType()
	{
		return "MessageStatusFilterChanged";
	}

	const StatusFilter statusFilter;
};

#endif	  // MESSAGE_STATUS_FILTER_CHANGED_H
