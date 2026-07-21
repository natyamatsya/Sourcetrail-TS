#ifndef MESSAGE_MOVE_IDE_CURSOR_H
#define MESSAGE_MOVE_IDE_CURSOR_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "FilePath.h"
#endif

SRCTRL_EXPORT class MessageMoveIDECursor: public Message<MessageMoveIDECursor>
{
public:
	MessageMoveIDECursor(const FilePath& filePath, const unsigned int row, const unsigned int column)
		: filePath(filePath), row(row), column(column)
	{
	}

	static const std::string getStaticType()
	{
		return "MessageMoveIDECursor";
	}

	void print(std::ostream& os) const override
	{
		os << filePath.str() << ":" << row << ":" << column;
	}

	const FilePath filePath;
	const unsigned int row;
	const unsigned int column;
};

#endif	  // MESSAGE_MOVE_IDE_CURSOR_H