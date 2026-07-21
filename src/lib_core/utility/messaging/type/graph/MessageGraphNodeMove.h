#ifndef MESSAGE_GRAPH_NODE_MOVE_H
#define MESSAGE_GRAPH_NODE_MOVE_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TabIds.h"
#include "Vector2.h"
#include "types.h"
#endif

SRCTRL_EXPORT class MessageGraphNodeMove: public Message<MessageGraphNodeMove>
{
public:
	MessageGraphNodeMove(Id tokenId, const Vec2i& delta): tokenId(tokenId), delta(delta)
	{
		setSchedulerId(TabIds::currentTab());
	}

	static const std::string getStaticType()
	{
		return "MessageGraphNodeMove";
	}

	void print(std::ostream& os) const override
	{
		os << tokenId << " " << delta.toString();
	}

	const Id tokenId;
	const Vec2i delta;
};

#endif	  // MESSAGE_GRAPH_NODE_MOVE_H
