#ifndef MESSAGE_SHOW_SCOPE_H
#define MESSAGE_SHOW_SCOPE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TabIds.h"
#include "types.h"
#endif

SRCTRL_EXPORT class MessageShowScope: public Message<MessageShowScope>
{
public:
	MessageShowScope(Id scopeLocationId, bool showErrors)
		: scopeLocationId(scopeLocationId), showErrors(showErrors)
	{
		setSchedulerId(TabIds::currentTab());
	}

	static const std::string getStaticType()
	{
		return "MessageShowScope";
	}

	void print(std::ostream& os) const override
	{
		os << scopeLocationId;
	}

	const Id scopeLocationId;
	const bool showErrors;
};

#endif	  // MESSAGE_SHOW_SCOPE_H
