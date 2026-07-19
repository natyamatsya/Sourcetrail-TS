#ifndef MESSAGE_INDEXING_STARTED_H
#define MESSAGE_INDEXING_STARTED_H

#include "Message.h"

class MessageIndexingStarted: public Message<MessageIndexingStarted>
{
public:
	static const std::string getStaticType()
	{
		return "MessageIndexingStarted";
	}

	MessageIndexingStarted() = default;
};

#endif	  // MESSAGE_INDEXING_STARTED_H
