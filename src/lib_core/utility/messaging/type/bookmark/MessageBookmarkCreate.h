#ifndef MESSAGE_BOOKMARK_CREATE_H
#define MESSAGE_BOOKMARK_CREATE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessageBookmarkCreate: public Message<MessageBookmarkCreate>
{
public:
	MessageBookmarkCreate(Id nodeId = 0): nodeId(nodeId) {}

	static const std::string getStaticType()
	{
		return "MessageBookmarkCreate";
	}

	const Id nodeId;
};

#endif	  // MESSAGE_BOOKMARK_CREATE_H