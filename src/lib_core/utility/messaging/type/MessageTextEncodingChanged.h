#ifndef MESSAGE_TEXT_ENCODING_CHANGED_H
#define MESSAGE_TEXT_ENCODING_CHANGED_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessageTextEncodingChanged : public Message<MessageTextEncodingChanged>
{
public:
	MessageTextEncodingChanged(const std::string &encoding)
		: textEncoding(encoding)
	{
	}

	static const std::string getStaticType()
	{
		return "MessageTextEncodingChanged";
	}
	
	const std::string textEncoding;
};

#endif
