#ifndef MESSAGE_BOOKMARK_EDIT_H
#define MESSAGE_BOOKMARK_EDIT_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessageBookmarkEdit: public Message<MessageBookmarkEdit>
{
public:
	static const std::string getStaticType()
	{
		return "MessageBookmarkEdit";
	}
};

#endif	  // MESSAGE_BOOKMARK_EDIT_H
