#ifndef MESSAGE_ERRORS_FOR_FILE_H
#define MESSAGE_ERRORS_FOR_FILE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessageErrorsForFile: public Message<MessageErrorsForFile>
{
public:
	static const std::string getStaticType()
	{
		return "MessageErrorsForFile";
	}

	MessageErrorsForFile(const FilePath& file): file(file) {}

	const FilePath& file;
};

#endif	  // MESSAGE_ERRORS_FOR_FILE_H
