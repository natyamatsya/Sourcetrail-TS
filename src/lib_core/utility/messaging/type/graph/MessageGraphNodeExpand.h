#ifndef MESSAGE_GRAPH_NODE_EXPAND_H
#define MESSAGE_GRAPH_NODE_EXPAND_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TabIds.h"
#include "types.h"
#endif

SRCTRL_EXPORT class MessageGraphNodeExpand: public Message<MessageGraphNodeExpand>
{
public:
	MessageGraphNodeExpand(Id tokenId, bool expand, bool ignoreIfNotReplayed = false)
		: tokenId(tokenId), expand(expand), ignoreIfNotReplayed(ignoreIfNotReplayed)
	{
		setSchedulerId(TabIds::currentTab());
	}

	static const std::string getStaticType()
	{
		return "MessageGraphNodeExpand";
	}

	void print(std::ostream& os) const override
	{
		os << tokenId << " ";
		if (expand)
		{
			os << "expand";
		}
		else
		{
			os << "collapse";
		}
	}

	const Id tokenId;
	const bool expand;
	const bool ignoreIfNotReplayed;
};

#endif	  // MESSAGE_GRAPH_NODE_EXPAND_H
