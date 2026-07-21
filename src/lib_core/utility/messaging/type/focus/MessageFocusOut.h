#ifndef MESSAGE_FOCUS_OUT_H
#define MESSAGE_FOCUS_OUT_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <vector>

#include "TabIds.h"
#include "types.h"
#endif

SRCTRL_EXPORT class MessageFocusOut: public Message<MessageFocusOut>
{
public:
	MessageFocusOut(const std::vector<Id>& tokenIds): tokenIds(tokenIds)
	{
		setIsLogged(false);
		setSchedulerId(TabIds::currentTab());
	}

	static const std::string getStaticType()
	{
		return "MessageFocusOut";
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

#endif	  // MESSAGE_FOCUS_OUT_H
