#ifndef MESSAGE_DEACTIVATE_EDGE_H
#define MESSAGE_DEACTIVATE_EDGE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TabIds.h"
#endif

SRCTRL_EXPORT class MessageDeactivateEdge: public Message<MessageDeactivateEdge>
{
public:
	MessageDeactivateEdge(bool scrollToDefinition): scrollToDefinition(scrollToDefinition)
	{
		setSchedulerId(TabIds::currentTab());
	}

	static const std::string getStaticType()
	{
		return "MessageDeactivateEdge";
	}

	bool scrollToDefinition;
};

#endif	  // MESSAGE_DEACTIVATE_EDGE_H
