#ifndef MESSAGE_SCROLL_TO_LINE_H
#define MESSAGE_SCROLL_TO_LINE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "FilePath.h"
#include "TabIds.h"
#endif

SRCTRL_EXPORT class MessageScrollToLine: public Message<MessageScrollToLine>
{
public:
	MessageScrollToLine(const FilePath& filePath, size_t line): filePath(filePath), line(line)
	{
		setSchedulerId(TabIds::currentTab());
	}

	static const std::string getStaticType()
	{
		return "MessageScrollToLine";
	}

	void print(std::ostream& os) const override
	{
		os << filePath.str() << ":" << line;
	}

	const FilePath filePath;
	size_t line;
};

#endif	  // MESSAGE_SCROLL_TO_LINE_H
