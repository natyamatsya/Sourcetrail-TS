#ifndef MESSAGE_PROJECT_NEW_H
#define MESSAGE_PROJECT_NEW_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "FilePath.h"
#endif

SRCTRL_EXPORT class MessageProjectNew: public Message<MessageProjectNew>
{
public:
	MessageProjectNew(const FilePath& cdbPath): cdbPath(cdbPath) {}

	static const std::string getStaticType()
	{
		return "MessageProjectNew";
	}

	const FilePath cdbPath;
};

#endif	  // MESSAGE_PROJECT_NEW_H
