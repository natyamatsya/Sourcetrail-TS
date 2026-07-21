#ifndef MESSAGE_ACTIVATE_FILE_H
#define MESSAGE_ACTIVATE_FILE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "FilePath.h"
#include "TabIds.h"
#endif

SRCTRL_EXPORT class MessageActivateFile: public Message<MessageActivateFile>
{
public:
	MessageActivateFile(const FilePath& filePath, unsigned int line = 0)
		: filePath(filePath), line(line)
	{
		setSchedulerId(TabIds::currentTab());
	}

	static const std::string getStaticType()
	{
		return "MessageActivateFile";
	}

	void print(std::ostream& os) const override
	{
		os << filePath.str();
	}

	const FilePath filePath;
	unsigned int line;
};

#endif	  // MESSAGE_ACTIVATE_FILE_H
