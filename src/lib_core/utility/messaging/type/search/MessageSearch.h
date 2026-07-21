#ifndef MESSAGE_SEARCH_H
#define MESSAGE_SEARCH_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "NodeTypeSet.h"
#include "SearchMatch.h"
#include "TabIds.h"
#endif

SRCTRL_EXPORT class MessageSearch: public Message<MessageSearch>
{
public:
	static const std::string getStaticType()
	{
		return "MessageSearch";
	}

	MessageSearch(const std::vector<SearchMatch>& matches, NodeTypeSet acceptedNodeTypes)
		: acceptedNodeTypes(acceptedNodeTypes), m_matches(matches)
	{
		setSchedulerId(TabIds::currentTab());
	}

	const std::vector<SearchMatch>& getMatches() const
	{
		return m_matches;
	}

	void print(std::ostream& os) const override
	{
		for (const SearchMatch& match: m_matches)
		{
			os << " @" << match.name;
			for (Id id: match.tokenIds)
			{
				os << ' ' << id;
			}
		}
	}

	NodeTypeSet acceptedNodeTypes;

private:
	const std::vector<SearchMatch> m_matches;
};

#endif	  // MESSAGE_SEARCH_H
