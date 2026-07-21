#ifndef MESSAGE_BOOKMARK_DELETE_H
#define MESSAGE_BOOKMARK_DELETE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessageBookmarkDelete: public Message<MessageBookmarkDelete>
{
public:
	static const std::string getStaticType()
	{
		return "MessageBookmarkDelete";
	}
};

#endif	  // MESSAGE_BOOKMARK_DELETE_H
