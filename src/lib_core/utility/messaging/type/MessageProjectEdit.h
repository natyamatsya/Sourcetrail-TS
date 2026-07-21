#ifndef MESSAGE_PROJECT_EDIT_H
#define MESSAGE_PROJECT_EDIT_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessageProjectEdit: public Message<MessageProjectEdit>
{
public:
	MessageProjectEdit() = default;

	static const std::string getStaticType()
	{
		return "MessageProjectEdit";
	}
};

#endif	  // MESSAGE_PROJECT_EDIT_H
