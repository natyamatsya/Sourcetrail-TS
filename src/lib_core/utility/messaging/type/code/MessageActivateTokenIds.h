#ifndef MESSAGE_ACTIVATE_TOKEN_IDS_H
#define MESSAGE_ACTIVATE_TOKEN_IDS_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TabIds.h"
#include "types.h"
#endif

SRCTRL_EXPORT class MessageActivateTokenIds: public Message<MessageActivateTokenIds>
{
public:
	MessageActivateTokenIds(const std::vector<Id>& tokenIds): tokenIds(tokenIds)
	{
		setSchedulerId(TabIds::currentTab());
	}

	static const std::string getStaticType()
	{
		return "MessageActivateTokenIds";
	}

	void print(std::ostream& os) const override
	{
		for (const Id& id: tokenIds)
		{
			os << id << " ";
		}
	}

	const std::vector<Id> tokenIds;
};

#endif	  // MESSAGE_ACTIVATE_TOKEN_IDS_H
