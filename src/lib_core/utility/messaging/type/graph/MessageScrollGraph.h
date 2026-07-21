#ifndef MESSAGE_SCROLL_GRAPH_H
#define MESSAGE_SCROLL_GRAPH_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TabIds.h"
#endif

SRCTRL_EXPORT class MessageScrollGraph: public Message<MessageScrollGraph>
{
public:
	MessageScrollGraph(int xValue, int yValue): xValue(xValue), yValue(yValue)
	{
		setIsLogged(false);
		setSchedulerId(TabIds::currentTab());
	}

	static const std::string getStaticType()
	{
		return "MessageScrollGraph";
	}

	int xValue;
	int yValue;
};

#endif	  // MESSAGE_SCROLL_GRAPH_H
