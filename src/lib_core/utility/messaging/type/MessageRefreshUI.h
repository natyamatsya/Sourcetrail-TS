#ifndef MESSAGE_REFRESH_UI_H
#define MESSAGE_REFRESH_UI_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessageRefreshUI: public Message<MessageRefreshUI>
{
public:
	static const std::string getStaticType()
	{
		return "MessageRefreshUI";
	}

	MessageRefreshUI() = default;

	MessageRefreshUI& noStyleReload()
	{
		loadStyle = false;
		return *this;
	}

	MessageRefreshUI& afterIndexing()
	{
		isAfterIndexing = true;
		return *this;
	}

	void print(std::ostream& os) const override
	{
		if (loadStyle)
		{
			os << "reload style";
		}
	}

	bool loadStyle = true;
	bool isAfterIndexing = false;
};

#endif	  // MESSAGE_REFRESH_UI_H
