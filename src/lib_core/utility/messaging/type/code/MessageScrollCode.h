#ifndef MESSAGE_SCROLL_CODE_H
#define MESSAGE_SCROLL_CODE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TabIds.h"
#endif

SRCTRL_EXPORT class MessageScrollCode: public Message<MessageScrollCode>
{
public:
	MessageScrollCode(int value, bool inListMode): value(value), inListMode(inListMode)
	{
		setIsLogged(false);
		setSchedulerId(TabIds::currentTab());
	}

	static const std::string getStaticType()
	{
		return "MessageScrollCode";
	}

	int value;
	bool inListMode;
};

#endif	  // MESSAGE_SCROLL_CODE_H
