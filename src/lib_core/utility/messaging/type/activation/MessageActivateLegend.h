#ifndef MESSAGE_ACTIVATE_LEGEND_H
#define MESSAGE_ACTIVATE_LEGEND_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"
#include "MessageActivateBase.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "TabIds.h"
#endif

SRCTRL_EXPORT class MessageActivateLegend
	: public Message<MessageActivateLegend>
	, public MessageActivateBase
{
public:
	MessageActivateLegend()
	{
		setSchedulerId(TabIds::currentTab());
	}

	static const std::string getStaticType()
	{
		return "MessageActivateLegend";
	}

	std::vector<SearchMatch> getSearchMatches() const override
	{
		return {SearchMatch::createCommand(SearchMatch::CommandType::COMMAND_LEGEND)};
	}
};

#endif	  // MESSAGE_ACTIVATE_LEGEND_H
