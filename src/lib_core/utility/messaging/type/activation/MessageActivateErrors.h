#ifndef MESSAGE_ACTIVATE_ERRORS_H
#define MESSAGE_ACTIVATE_ERRORS_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"
#include "MessageActivateBase.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "ErrorFilter.h"
#include "TabIds.h"
#endif

SRCTRL_EXPORT class MessageActivateErrors
	: public Message<MessageActivateErrors>
	, public MessageActivateBase
{
public:
	static const std::string getStaticType()
	{
		return "MessageActivateErrors";
	}

	MessageActivateErrors(const ErrorFilter& filter, const FilePath& file = FilePath())
		: filter(filter), file(file)
	{
		setSchedulerId(TabIds::currentTab());
	}

	std::vector<SearchMatch> getSearchMatches() const override
	{
		std::vector<SearchMatch> matches = {SearchMatch::createCommand(SearchMatch::CommandType::COMMAND_ERROR)};
		if (!file.empty())
		{
			SearchMatch match;
			match.name = match.text = file.fileName();
			match.searchType = SearchMatch::SearchType::SEARCH_TOKEN;
			match.nodeType = NodeType(NODE_FILE);
			matches.push_back(match);
		}
		return matches;
	}

	const ErrorFilter filter;
	const FilePath file;
};

#endif	  // MESSAGE_ACTIVATE_ERRORS_H
