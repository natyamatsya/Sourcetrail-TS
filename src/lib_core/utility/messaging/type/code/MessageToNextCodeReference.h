#ifndef MESSAGE_TO_NEXT_CODE_REFERENCE_H
#define MESSAGE_TO_NEXT_CODE_REFERENCE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TabIds.h"
#endif

SRCTRL_EXPORT class MessageToNextCodeReference: public Message<MessageToNextCodeReference>
{
public:
	MessageToNextCodeReference(const FilePath& filePath, size_t lineNumber, size_t columnNumber, bool next)
		: filePath(filePath), lineNumber(lineNumber), columnNumber(columnNumber), next(next)
	{
		setSchedulerId(TabIds::currentTab());
	}

	static const std::string getStaticType()
	{
		return "MessageToNextCodeReference";
	}

	void print(std::ostream& os) const override
	{
		os << filePath.str() << ' ' << lineNumber << ':' << columnNumber << ' ';

		if (next)
		{
			os << "next";
		}
		else
		{
			os << "previous";
		}
	}

	const FilePath filePath;
	const size_t lineNumber;
	const size_t columnNumber;
	const bool next;
};

#endif	  // MESSAGE_TO_NEXT_CODE_REFERENCE_H
