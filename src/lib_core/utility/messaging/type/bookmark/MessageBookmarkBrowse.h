#ifndef MESSAGE_BOOKMARKS_BROWSE_H
#define MESSAGE_BOOKMARKS_BROWSE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "Bookmark.h"
#endif

SRCTRL_EXPORT class MessageBookmarkBrowse: public Message<MessageBookmarkBrowse>
{
public:
	MessageBookmarkBrowse(
		Bookmark::BookmarkFilter filter = Bookmark::BookmarkFilter::FILTER_UNKNOWN,
		Bookmark::BookmarkOrder order = Bookmark::BookmarkOrder::ORDER_NONE)
		: filter(filter), order(order)
	{
	}

	static const std::string getStaticType()
	{
		return "MessageBookmarkBrowse";
	}

	const Bookmark::BookmarkFilter filter;
	const Bookmark::BookmarkOrder order;
};

#endif	  // MESSAGE_BOOKMARKS_BROWSE_H
