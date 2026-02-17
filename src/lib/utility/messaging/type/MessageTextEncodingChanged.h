#ifndef MESSAGE_TEXT_ENCODING_CHANGED_H
#define MESSAGE_TEXT_ENCODING_CHANGED_H

#include "Message.h"

class MessageTextEncodingChanged : public Message<MessageTextEncodingChanged>
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
