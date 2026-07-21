#ifndef MESSAGE_ACTIVATE_ALL_H
#define MESSAGE_ACTIVATE_ALL_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"
#include "MessageActivateBase.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "NodeTypeSet.h"
#include "TabIds.h"
#endif

SRCTRL_EXPORT class MessageActivateOverview
	: public Message<MessageActivateOverview>
	, public MessageActivateBase
{
public:
	static const std::string getStaticType()
	{
		return "MessageActivateOverview";
	}

	MessageActivateOverview(NodeTypeSet acceptedNodeTypes = NodeTypeSet::all())
		: acceptedNodeTypes(acceptedNodeTypes)
	{
		setIsParallel(true);
		setSchedulerId(TabIds::currentTab());
	}

	std::vector<SearchMatch> getSearchMatches() const override
	{
		if (acceptedNodeTypes != NodeTypeSet::all())
		{
			return SearchMatch::createCommandsForNodeTypes(acceptedNodeTypes);
		}
		return {SearchMatch::createCommand(SearchMatch::CommandType::COMMAND_ALL)};
	}

	NodeTypeSet acceptedNodeTypes;
};

#endif	  // MESSAGE_ACTIVATE_ALL_H
