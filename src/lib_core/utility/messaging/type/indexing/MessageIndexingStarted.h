#ifndef MESSAGE_INDEXING_STARTED_H
#define MESSAGE_INDEXING_STARTED_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessageIndexingStarted: public Message<MessageIndexingStarted>
{
public:
	static const std::string getStaticType()
	{
		return "MessageIndexingStarted";
	}

	MessageIndexingStarted() = default;
};

#endif	  // MESSAGE_INDEXING_STARTED_H
