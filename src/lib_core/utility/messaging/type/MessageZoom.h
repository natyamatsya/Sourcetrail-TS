#ifndef MESSAGE_ZOOM_H
#define MESSAGE_ZOOM_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessageZoom: public Message<MessageZoom>
{
public:
	MessageZoom(bool zoomIn): zoomIn(zoomIn) {}

	static const std::string getStaticType()
	{
		return "MessageZoom";
	}

	void print(std::ostream& os) const override
	{
		if (zoomIn)
		{
			os << "in";
		}
		else
		{
			os << "out";
		}
	}

	const bool zoomIn;
};

#endif	  // MESSAGE_ZOOM_H
