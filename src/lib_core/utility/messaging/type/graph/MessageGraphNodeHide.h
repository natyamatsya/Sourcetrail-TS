#ifndef MESSAGE_GRAPH_NODE_HIDE_H
#define MESSAGE_GRAPH_NODE_HIDE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TabIds.h"
#include "types.h"
#endif

SRCTRL_EXPORT class MessageGraphNodeHide: public Message<MessageGraphNodeHide>
{
public:
	MessageGraphNodeHide(Id tokenId): tokenId(tokenId)
	{
		setSchedulerId(TabIds::currentTab());
	}

	static const std::string getStaticType()
	{
		return "MessageGraphNodeHide";
	}

	void print(std::ostream& os) const override
	{
		os << tokenId;
	}

	const Id tokenId;
};

#endif	  // MESSAGE_GRAPH_NODE_HIDE_H
