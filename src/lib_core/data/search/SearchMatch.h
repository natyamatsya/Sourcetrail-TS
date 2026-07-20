#ifndef SEARCH_MATCH_H
#define SEARCH_MATCH_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "LogFacade.h"
#include "Node.h"
#include "NodeTypeSet.h"
#include "types.h"

// NodeTypeSet is not modularized; the srctrl.data wrapper supplies it in the GMF for the module build,
// so its forward decl (and full include above) is skipped in the purview.
class NodeTypeSet;
#endif

// SearchMatch is used to display the search result in the UI
SRCTRL_EXPORT struct SearchMatch
{
	static constexpr char FULLTEXT_SEARCH_CHARACTER = '?';

	enum class SearchType
	{
		SEARCH_NONE,
		SEARCH_TOKEN,
		SEARCH_COMMAND,
		SEARCH_OPERATOR,
		SEARCH_FULLTEXT
	};

	enum class CommandType
	{
		COMMAND_ALL,
		COMMAND_ERROR,
		COMMAND_NODE_FILTER,
		COMMAND_LEGEND
	};

	static void log(const std::vector<SearchMatch>& matches, const std::string& query);

	static std::string getSearchTypeName(SearchType type);
	static std::string searchMatchesToString(const std::vector<SearchMatch>& matches);

	static SearchMatch createCommand(CommandType type);
	static std::vector<SearchMatch> createCommandsForNodeTypes(NodeTypeSet types);
	static std::string getCommandName(CommandType type);

	SearchMatch();
	SearchMatch(const std::string& query);

	bool operator<(const SearchMatch& other) const;
	bool operator==(const SearchMatch& other) const;

	static std::size_t getTextSizeForSorting(const std::string* str);

	bool isValid() const;
	bool isFilterCommand() const;

	void print(std::ostream& ostream) const;

	std::string getFullName() const;
	std::string getSearchTypeName() const;
	CommandType getCommandType() const;

	std::string name;

	std::string text;
	std::string subtext;

	std::vector<Id> tokenIds;
	std::vector<NameHierarchy> tokenNames;

	std::string typeName;

	NodeType nodeType;
	SearchType searchType;
	std::vector<std::size_t> indices;

	int score = 0;
	bool hasChildren = false;
};

// Whether this match is the (a) main function. The NameHierarchy-side main-function helpers
// live in utilityMainFunction.h (srctrl.data:name); this overload sits with SearchMatch so the
// search layer needs no dependency on them.
SRCTRL_EXPORT bool isMainFunction(const SearchMatch &match);

#include "SearchMatch.inl"

#endif	  // SEARCH_MATCH_H
