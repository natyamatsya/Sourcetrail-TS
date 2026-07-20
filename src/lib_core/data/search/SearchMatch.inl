// Inline member definitions for SearchMatch.h (included at the end of that header). All members are
// inline because an out-of-line member of an exported struct does not resolve for module importers.
// The dependencies used below (<sstream>, NodeTypeSet, srctrl::log) come from SearchMatch.h's guarded
// include block in the header build, and from the srctrl.data wrapper's imports in the module build.

#pragma once

inline void SearchMatch::log(const std::vector<SearchMatch>& matches, const std::string& query)
{
	using enum SearchMatch::SearchType;
	using enum SearchMatch::CommandType;
	std::stringstream ss;
	ss << std::endl << matches.size() << " matches for \"" << query << "\":" << std::endl;

	for (const SearchMatch& match: matches)
	{
		match.print(ss);
	}

	srctrl::log::info(ss.str());
}

inline std::string SearchMatch::getSearchTypeName(SearchType type)
{
	using enum SearchMatch::SearchType;
	using enum SearchMatch::CommandType;
	switch (type)
	{
	case SEARCH_NONE:
		return "none";
	case SEARCH_TOKEN:
		return "token";
	case SEARCH_COMMAND:
		return "command";
	case SEARCH_OPERATOR:
		return "operator";
	case SEARCH_FULLTEXT:
		return "fulltext";
	}

	return "none";
}

inline std::string SearchMatch::searchMatchesToString(const std::vector<SearchMatch>& matches)
{
	std::stringstream ss;

	for (const SearchMatch& match: matches)
	{
		ss << '@' << match.getFullName() << ':'
		   << getReadableNodeKindString(match.nodeType.getKind()) << ' ';
	}

	return ss.str();
}

inline SearchMatch SearchMatch::createCommand(CommandType type)
{
	using enum SearchMatch::SearchType;
	using enum SearchMatch::CommandType;
	SearchMatch match;
	match.name = getCommandName(type);
	match.text = match.name;
	match.typeName = "command";
	match.searchType = SEARCH_COMMAND;
	return match;
}

inline std::vector<SearchMatch> SearchMatch::createCommandsForNodeTypes(NodeTypeSet types)
{
	using enum SearchMatch::SearchType;
	using enum SearchMatch::CommandType;
	std::vector<SearchMatch> matches;

	for (const NodeType& type: types.getNodeTypes())
	{
		SearchMatch match;
		match.name = type.getReadableTypeString();
		match.text = match.name;
		match.typeName = "filter";
		match.searchType = SEARCH_COMMAND;
		match.nodeType = type;
		matches.push_back(match);
	}

	return matches;
}

inline std::string SearchMatch::getCommandName(CommandType type)
{
	using enum SearchMatch::SearchType;
	using enum SearchMatch::CommandType;
	switch (type)
	{
	case COMMAND_ALL:
		return "overview";
	case COMMAND_ERROR:
		return "error";
	case COMMAND_NODE_FILTER:
		return "node_filter";
	case COMMAND_LEGEND:
		return "legend";
	}

	return "none";
}

inline SearchMatch::SearchMatch()
	:  nodeType(NODE_SYMBOL), searchType(SearchMatch::SearchType::SEARCH_NONE)
{
}

inline SearchMatch::SearchMatch(const std::string& query)
	: name(query)
	, text(query)
	,
	 nodeType(NODE_SYMBOL)
	, searchType(SearchMatch::SearchType::SEARCH_NONE)

{
	using enum SearchMatch::SearchType;
	using enum SearchMatch::CommandType;
	tokenNames.emplace_back(query, NameDelimiterType::UNKNOWN);
}

inline bool SearchMatch::operator<(const SearchMatch& other) const
{
	// score
	if (score > other.score)
	{
		return true;
	}
	else if (score < other.score)
	{
		return false;
	}

	const std::string* str = &text;
	const std::string* otherStr = &other.text;
	if (*str == *otherStr)
	{
		str = &name;
		otherStr = &other.name;
	}

	std::size_t size = getTextSizeForSorting(str);
	std::size_t otherSize = getTextSizeForSorting(otherStr);

	// text size
	if (size < otherSize)
	{
		return true;
	}
	else if (size > otherSize)
	{
		return false;
	}
	else if (str->size() < otherStr->size())
	{
		return true;
	}
	else if (str->size() > otherStr->size())
	{
		return false;
	}

	// lower case
	for (std::size_t i = 0; i < str->size(); i++)
	{
		if (tolower(str->at(i)) != tolower(otherStr->at(i)))
		{
			return tolower(str->at(i)) < tolower(otherStr->at(i));
		}
		else
		{
			// alphabetical
			if (str->at(i) < otherStr->at(i))
			{
				return true;
			}
			else if (str->at(i) > otherStr->at(i))
			{
				return false;
			}
		}
	}

	return getSearchTypeName() < other.getSearchTypeName();
}

inline bool SearchMatch::operator==(const SearchMatch& other) const
{
	return text == other.text && searchType == other.searchType;
}

inline std::size_t SearchMatch::getTextSizeForSorting(const std::string* str)
{
	// check if templated symbol and only use size up to template stuff
	std::size_t pos = str->find('<');
	if (pos != std::string::npos)
	{
		return pos;
	}

	return str->size();
}

inline bool SearchMatch::isValid() const
{
	using enum SearchMatch::SearchType;
	using enum SearchMatch::CommandType;
	return searchType != SEARCH_NONE;
}

inline bool SearchMatch::isFilterCommand() const
{
	using enum SearchMatch::SearchType;
	using enum SearchMatch::CommandType;
	return searchType == SEARCH_COMMAND && getCommandType() == COMMAND_NODE_FILTER;
}

inline void SearchMatch::print(std::ostream& ostream) const
{
	ostream << name << std::endl << '\t';
	std::size_t i = 0;
	for (std::size_t index: indices)
	{
		while (i < index)
		{
			i++;
			ostream << ' ';
		}
		ostream << '^';
		i++;
	}
	ostream << std::endl;
}

inline std::string SearchMatch::getFullName() const
{
	using enum SearchMatch::SearchType;
	using enum SearchMatch::CommandType;
	if (searchType == SEARCH_TOKEN && nodeType.isFile())
	{
		return text;
	}

	return name;
}

inline std::string SearchMatch::getSearchTypeName() const
{
	return getSearchTypeName(searchType);
}

inline SearchMatch::CommandType SearchMatch::getCommandType() const
{
	using enum SearchMatch::SearchType;
	using enum SearchMatch::CommandType;
	if (name == "overview")
	{
		return COMMAND_ALL;
	}
	else if (name == "error")
	{
		return COMMAND_ERROR;
	}
	else if (name == "legend")
	{
		return COMMAND_LEGEND;
	}

	return COMMAND_NODE_FILTER;
}

inline bool isMainFunction(const SearchMatch &match)
{
	// Mirrors utilityMainFunction's MAIN_NAME ("main").
	return match.nodeType.getKind() == NODE_FUNCTION && match.tokenNames.size() != 0 &&
		match.tokenNames[0].getRawName() == "main";
}
