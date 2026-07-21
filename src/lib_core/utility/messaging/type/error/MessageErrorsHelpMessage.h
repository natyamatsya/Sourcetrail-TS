#ifndef MESSAGE_ERRORS_HELP_MESSAGE_H
#define MESSAGE_ERRORS_HELP_MESSAGE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessageErrorsHelpMessage: public Message<MessageErrorsHelpMessage>
{
public:
	static const std::string getStaticType()
	{
		return "MessageErrorsHelpMessage";
	}

	MessageErrorsHelpMessage(bool force = false): force(force) {}

	const bool force;
};

#endif	  // MESSAGE_ERRORS_HELP_MESSAGE_H
