#ifndef MESSAGE_FIND_H
#define MESSAGE_FIND_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TabIds.h"
#endif

SRCTRL_EXPORT class MessageFind: public Message<MessageFind>
{
public:
	MessageFind(bool fulltext = false): findFulltext(fulltext)
	{
		setSchedulerId(TabIds::currentTab());
	}

	static const std::string getStaticType()
	{
		return "MessageFind";
	}

	bool findFulltext;
};

#endif	  // MESSAGE_FIND_H
