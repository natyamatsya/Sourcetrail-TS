#ifndef MESSAGE_REFRESH_H
#define MESSAGE_REFRESH_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessageRefresh: public Message<MessageRefresh>
{
public:
	static const std::string getStaticType()
	{
		return "MessageRefresh";
	}

	MessageRefresh() = default;

	MessageRefresh& refreshAll()
	{
		all = true;
		return *this;
	}

	void print(std::ostream& os) const override
	{
		if (all)
		{
			os << "all";
		}
	}

	bool all = false;
};

#endif	  // MESSAGE_REFRESH_H
