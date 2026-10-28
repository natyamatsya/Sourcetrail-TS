#ifndef MESSAGE_OPEN_IN_EDITOR_H
#define MESSAGE_OPEN_IN_EDITOR_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "FilePath.h"
#endif

// "Open in Editor": the connected IDE plugin moves its cursor there, as Show in IDE does;
// without one, the editor command (ApplicationSettings::getEditorCommand) opens the file.
SRCTRL_EXPORT class MessageOpenInEditor: public Message<MessageOpenInEditor>
{
public:
	MessageOpenInEditor(const FilePath& filePath, const unsigned int line, const unsigned int column)
		: filePath(filePath), line(line), column(column)
	{
	}

	static const std::string getStaticType()
	{
		return "MessageOpenInEditor";
	}

	void print(std::ostream& os) const override
	{
		os << filePath.str() << ":" << line << ":" << column;
	}

	const FilePath filePath;
	const unsigned int line;
	const unsigned int column;
};

#endif	  // MESSAGE_OPEN_IN_EDITOR_H
