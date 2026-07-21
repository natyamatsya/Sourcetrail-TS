#ifndef MESSAGE_SHOW_ERROR_H
#define MESSAGE_SHOW_ERROR_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TabIds.h"
#endif

SRCTRL_EXPORT class MessageShowError: public Message<MessageShowError>
{
public:
	static const std::string getStaticType()
	{
		return "MessageShowError";
	}

	MessageShowError(Id errorId): errorId(errorId)
	{
		setSchedulerId(TabIds::currentTab());
	}

	const Id errorId;
};

#endif	  // MESSAGE_SHOW_ERROR_H
