#ifndef MESSAGE_FOCUS_IN_H
#define MESSAGE_FOCUS_IN_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <vector>

#include "TabIds.h"
#include "TooltipOrigin.h"
#include "types.h"
#endif

SRCTRL_EXPORT class MessageFocusIn: public Message<MessageFocusIn>
{
public:
	MessageFocusIn(const std::vector<Id>& tokenIds, TooltipOrigin origin = TooltipOrigin::TOOLTIP_ORIGIN_NONE)
		: tokenIds(tokenIds), origin(origin)
	{
		setIsLogged(false);
		setSchedulerId(TabIds::currentTab());
	}

	static const std::string getStaticType()
	{
		return "MessageFocusIn";
	}

	void print(std::ostream& os) const override
	{
		for (const Id& id: tokenIds)
		{
			os << id << " ";
		}
	}

	const std::vector<Id> tokenIds;
	const TooltipOrigin origin;
};

#endif	  // MESSAGE_FOCUS_IN_H
