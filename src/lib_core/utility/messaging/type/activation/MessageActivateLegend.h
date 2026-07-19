#ifndef MESSAGE_ACTIVATE_LEGEND_H
#define MESSAGE_ACTIVATE_LEGEND_H

#include "Message.h"
#include "MessageActivateBase.h"
#include "TabIds.h"

class MessageActivateLegend
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
