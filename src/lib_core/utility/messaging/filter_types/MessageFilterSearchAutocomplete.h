#ifndef MESSAGE_FILTER_SEARCH_AUTOCOMPLETE_H
#define MESSAGE_FILTER_SEARCH_AUTOCOMPLETE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "MessageFilter.h"
#include "MessageSearchAutocomplete.h"


SRCTRL_EXPORT class MessageFilterSearchAutocomplete: public MessageFilter
{
	void filter(MessageQueue::MessageBufferType* messageBuffer) override
	{
		if (messageBuffer->size() < 2)
		{
			return;
		}

		MessageBase* message = messageBuffer->front().get();
		if (message->getType() == MessageSearchAutocomplete::getStaticType())
		{
			for (auto it = messageBuffer->begin() + 1; it != messageBuffer->end(); it++)
			{
				if ((*it)->getType() == MessageSearchAutocomplete::getStaticType())
				{
					messageBuffer->pop_front();
					return;
				}
			}
		}
	}
};

#endif	  // MESSAGE_FILTER_SEARCH_AUTOCOMPLETE_H
