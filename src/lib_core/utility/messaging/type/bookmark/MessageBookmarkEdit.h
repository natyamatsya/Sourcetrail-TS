#ifndef MESSAGE_BOOKMARK_EDIT_H
#define MESSAGE_BOOKMARK_EDIT_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessageBookmarkEdit: public Message<MessageBookmarkEdit>
{
public:
	explicit MessageBookmarkEdit(std::uint64_t bookmarkId = 0): bookmarkId(bookmarkId) {}

	static const std::string getStaticType()
	{
		return "MessageBookmarkEdit";
	}

	const std::uint64_t bookmarkId;	// 0: the bookmark of what is active
};

#endif	  // MESSAGE_BOOKMARK_EDIT_H
