#ifndef MESSAGE_FLUSH_UPDATES_H
#define MESSAGE_FLUSH_UPDATES_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TabIds.h"
#endif

SRCTRL_EXPORT class MessageFlushUpdates: public Message<MessageFlushUpdates>
{
public:
	MessageFlushUpdates(bool keepsContent = false)
	{
		setKeepContent(keepsContent);
		setSchedulerId(TabIds::currentTab());
	}

	static const std::string getStaticType()
	{
		return "MessageFlushUpdates";
	}
};

#endif	  // MESSAGE_FLUSH_UPDATES_H
