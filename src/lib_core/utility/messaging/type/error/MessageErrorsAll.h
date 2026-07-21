#ifndef MESSAGE_ERRORS_ALL_H
#define MESSAGE_ERRORS_ALL_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessageErrorsAll: public Message<MessageErrorsAll>
{
public:
	static const std::string getStaticType()
	{
		return "MessageErrorsAll";
	}

	MessageErrorsAll() = default;
};

#endif	  // MESSAGE_ERRORS_ALL_H
