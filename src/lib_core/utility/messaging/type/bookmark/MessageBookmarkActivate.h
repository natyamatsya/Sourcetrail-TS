#ifndef MESSAGE_BOOKMARK_ACTIVATE_H
#define MESSAGE_BOOKMARK_ACTIVATE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "Bookmark.h"
#endif

SRCTRL_EXPORT class MessageBookmarkActivate: public Message<MessageBookmarkActivate>
{
public:
	MessageBookmarkActivate(const std::shared_ptr<Bookmark>& bookmark): bookmark(bookmark) {}

	static const std::string getStaticType()
	{
		return "MessageBookmarkActivate";
	}

	const std::shared_ptr<Bookmark> bookmark;
};

#endif	  // MESSAGE_BOOKMARK_ACTIVATE_H
