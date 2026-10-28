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

	// A person's refresh: it starts at once where one mode does (only "all" is
	// possible, or nothing in the project's settings changed), and only asks for the
	// mode when the settings changed. Without it the mode choice opens, as an
	// agent's refresh expects.
	MessageRefresh& direct()
	{
		isDirect = true;
		return *this;
	}

	void print(std::ostream& os) const override
	{
		if (all)
		{
			os << "all";
		}
		if (isDirect)
		{
			os << (all ? " direct" : "direct");
		}
	}

	bool all = false;
	bool isDirect = false;
};

#endif	  // MESSAGE_REFRESH_H
