#include "Catch2.hpp"

#include "NameHierarchy.h"
#include "SearchMatch.h"
#include "TabSession.h"

namespace
{
NameHierarchy cxxName(const std::vector<std::string>& parts)
{
	NameHierarchy name(NameDelimiterType::CXX);
	for (const std::string& part: parts)
	{
		name.push(part);
	}
	return name;
}

SearchMatch symbol(const std::vector<std::string>& parts, Id id)
{
	SearchMatch match;
	match.name = match.text = cxxName(parts).getQualifiedName();
	match.searchType = SearchMatch::SearchType::SEARCH_TOKEN;
	match.tokenNames = {cxxName(parts)};
	match.tokenIds = {id};
	return match;
}

TabId tab(unsigned id)
{
	return static_cast<TabId>(id);
}

// Finds what the "index" still knows: fmt::format and fmt::vformat, not fmt::gone.
std::vector<SearchMatch> findKnown(const std::vector<NameHierarchy>& names)
{
	std::vector<SearchMatch> matches;
	for (const NameHierarchy& name: names)
	{
		const std::string qualified = name.getQualifiedName();
		if (qualified == "fmt::format")
		{
			matches.push_back(symbol({"fmt", "format"}, 10));
		}
		else if (qualified == "fmt::vformat")
		{
			matches.push_back(symbol({"fmt", "vformat"}, 11));
		}
	}
	return matches;
}
}	 // namespace

TEST_CASE("tab session keeps what each tab showed, the active tab, and a bounded history")
{
	tab_session::Recorder recorder;
	REQUIRE(recorder.record(tab(5), {symbol({"fmt", "format"}, 1)}));
	REQUIRE(recorder.record(tab(5), {symbol({"fmt", "vformat"}, 2)}));
	REQUIRE_FALSE(recorder.record(tab(5), {symbol({"fmt", "vformat"}, 2)}));	// the same again
	REQUIRE(recorder.record(tab(7), {SearchMatch::createCommand(SearchMatch::CommandType::COMMAND_ALL)}));
	recorder.activate(tab(7));

	const tab_session::Session session = recorder.session();
	REQUIRE(session.tabs.size() == 2);
	REQUIRE(session.activeTab == 1);
	REQUIRE(session.tabs[0].history.size() == 2);
	REQUIRE(session.tabs[0].history.back().front().names.size() == 1);
	REQUIRE(session.tabs[1].history.back().front().command == SearchMatch::getCommandName(SearchMatch::CommandType::COMMAND_ALL));

	for (int i = 0; i < 30; i++)
	{
		recorder.record(tab(5), {symbol({"ns", "f" + std::to_string(i)}, Id(100 + i))});
	}
	REQUIRE(recorder.session().tabs[0].history.size() == tab_session::maxHistory);

	recorder.remove(tab(7));
	REQUIRE(recorder.session().tabs.size() == 1);
	REQUIRE(recorder.session().activeTab == 0);
}

TEST_CASE("tab session moves a symbol shown again to the end of the history")
{
	tab_session::Recorder recorder;
	recorder.record(tab(1), {symbol({"a"}, 1)});
	recorder.record(tab(1), {symbol({"b"}, 2)});
	recorder.record(tab(1), {symbol({"a"}, 1)});	// Back

	const std::vector<tab_session::Entry> history = recorder.session().tabs[0].history;
	REQUIRE(history.size() == 2);
	REQUIRE(NameHierarchy::deserialize(history.back().front().names.front()).getQualifiedName() == "a");
}

TEST_CASE("tab session leaves out what cannot be shown again")
{
	SearchMatch fullText;
	fullText.searchType = SearchMatch::SearchType::SEARCH_FULLTEXT;
	fullText.name = "?needle";
	REQUIRE_FALSE(tab_session::toEntry({fullText}).has_value());
	REQUIRE_FALSE(tab_session::toEntry({}).has_value());

	tab_session::Recorder recorder;
	REQUIRE_FALSE(recorder.record(tab(1), {fullText}));
	REQUIRE(recorder.session().tabs.empty());
}

TEST_CASE("tab session survives its JSON")
{
	tab_session::Recorder recorder;
	recorder.record(tab(1), {symbol({"fmt", "format"}, 1)});
	recorder.record(tab(1), {symbol({"fmt", "vformat"}, 2), symbol({"fmt", "format"}, 1)});
	recorder.record(tab(2), {SearchMatch::createCommand(SearchMatch::CommandType::COMMAND_ERROR)});
	recorder.activate(tab(2));

	const tab_session::Session session = recorder.session();
	const auto parsed = tab_session::fromJson(tab_session::toJson(session));
	REQUIRE(parsed.has_value());
	REQUIRE(parsed->activeTab == 1);
	REQUIRE(parsed->tabs.size() == 2);
	REQUIRE(parsed->tabs[0].history == session.tabs[0].history);
	REQUIRE(parsed->tabs[1].history == session.tabs[1].history);
}

TEST_CASE("tab session refuses what it cannot read")
{
	REQUIRE(tab_session::fromJson("not json").error() == tab_session::SessionError::Malformed);
	REQUIRE(tab_session::fromJson(R"({"version":99,"tabs":[]})").error() == tab_session::SessionError::UnknownVersion);
}

TEST_CASE("tab session shows again what is still there")
{
	const tab_session::Entry entry {
		{"", {NameHierarchy::serialize(cxxName({"fmt", "format"}))}},
		{"", {NameHierarchy::serialize(cxxName({"fmt", "gone"}))}},
		{SearchMatch::getCommandName(SearchMatch::CommandType::COMMAND_LEGEND), {}},
		{"no such command", {}}};

	const std::vector<SearchMatch> matches = tab_session::toMatches(entry, findKnown);
	REQUIRE(matches.size() == 2);
	REQUIRE(matches[0].tokenIds == std::vector<Id> {10});
	REQUIRE(matches[1].getCommandType() == SearchMatch::CommandType::COMMAND_LEGEND);

	const tab_session::Entry gone {{"", {NameHierarchy::serialize(cxxName({"fmt", "gone"}))}}};
	REQUIRE(tab_session::toMatches(gone, findKnown).empty());
}
