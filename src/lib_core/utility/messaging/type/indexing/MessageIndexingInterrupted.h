#ifndef MESSAGE_INDEXING_INTERRUPTED_H
#define MESSAGE_INDEXING_INTERRUPTED_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessageIndexingInterrupted: public Message<MessageIndexingInterrupted>
{
public:
	static const std::string getStaticType()
	{
		return "MessageIndexingInterrupted";
	}

	MessageIndexingInterrupted()
	{
		setSendAsTask(false);
	}
};

#endif	  // MESSAGE_INDEXING_INTERRUPTED_H
