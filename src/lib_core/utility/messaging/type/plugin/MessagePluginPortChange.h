#ifndef MESSAGE_PLUGIN_PORT_CHANGE_H
#define MESSAGE_PLUGIN_PORT_CHANGE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"


SRCTRL_EXPORT class MessagePluginPortChange: public Message<MessagePluginPortChange>
{
public:
	MessagePluginPortChange() = default;

	static const std::string getStaticType()
	{
		return "MessagePluginPortChange";
	}
};

#endif	  // MESSAGE_PLUGIN_PORT_CHANGE_H
