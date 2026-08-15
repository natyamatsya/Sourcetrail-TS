#ifndef MESSAGE_ACTIVATE_BOUNDARIES_H
#define MESSAGE_ACTIVATE_BOUNDARIES_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"
#include "MessageActivateBase.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TabIds.h"
#endif

// Shows every language boundary in the project: the contract atoms, and the
// declarations bound to them. Reached from the search field as the "boundary"
// command, which is why it carries a SearchMatch like the other commands.
//
// It is a command rather than a node-type filter because a boundary is not a
// kind of node -- NodeTypeSet's matchers only ever see a NodeType, and what
// makes a node a boundary is its language mask. See
// context/DESIGN_XLANG_BOUNDARIES.md.
SRCTRL_EXPORT class MessageActivateBoundaries
	: public Message<MessageActivateBoundaries>
	, public MessageActivateBase
{
public:
	MessageActivateBoundaries()
	{
		setSchedulerId(TabIds::currentTab());
	}

	static const std::string getStaticType()
	{
		return "MessageActivateBoundaries";
	}

	std::vector<SearchMatch> getSearchMatches() const override
	{
		return {SearchMatch::createCommand(SearchMatch::CommandType::COMMAND_BOUNDARY)};
	}
};

#endif	  // MESSAGE_ACTIVATE_BOUNDARIES_H
