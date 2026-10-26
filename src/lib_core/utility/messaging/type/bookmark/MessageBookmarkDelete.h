#ifndef MESSAGE_BOOKMARK_DELETE_H
#define MESSAGE_BOOKMARK_DELETE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessageBookmarkDelete: public Message<MessageBookmarkDelete>
{
public:
	explicit MessageBookmarkDelete(std::uint64_t bookmarkId = 0): bookmarkId(bookmarkId) {}

	static const std::string getStaticType()
	{
		return "MessageBookmarkDelete";
	}

	// 0: the bookmark of what is active, deleted at once (the bookmark button asked);
	// else that bookmark, after asking whether to (as the browser's delete button does).
	const std::uint64_t bookmarkId;
};

#endif	  // MESSAGE_BOOKMARK_DELETE_H
