#ifndef MESSAGE_CLOSE_PROJECT_H
#define MESSAGE_CLOSE_PROJECT_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "RefreshInfo.h"

#include "FilePath.h"
#endif

SRCTRL_EXPORT class MessageCloseProject: public Message<MessageCloseProject>
{
public:
	MessageCloseProject() = default;

	static const std::string getStaticType()
	{
		return "MessageCloseProject";
	}

	void print(std::ostream&  /*os*/) const override {}
};

#endif	  // MESSAGE_CLOSE_PROJECT_H
