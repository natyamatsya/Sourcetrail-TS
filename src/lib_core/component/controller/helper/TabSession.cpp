#include "TabSession.h"

#include <algorithm>
#include <array>

#include <glaze/glaze.hpp>

// glaze reflects these for JSON; MSVC cannot reflect types in an anonymous namespace.
namespace tab_session_json
{
struct Shown
{
	std::optional<std::string> command;
	std::vector<std::string> names;
};

struct Tab
{
	std::vector<std::vector<Shown>> history;
};

struct Session
{
	int version = 0;
	std::vector<Tab> tabs;
	std::size_t active_tab = 0;
};
}	 // namespace tab_session_json

namespace tab_session
{
namespace
{
constexpr int sessionVersion = 1;

// The commands a tab can show again.
constexpr std::array restorableCommands {
	SearchMatch::CommandType::COMMAND_ALL,
	SearchMatch::CommandType::COMMAND_ERROR,
	SearchMatch::CommandType::COMMAND_LEGEND,
	SearchMatch::CommandType::COMMAND_BOUNDARY};

std::optional<SearchMatch::CommandType> commandNamed(const std::string& name)
{
	for (const SearchMatch::CommandType type: restorableCommands)
	{
		if (SearchMatch::getCommandName(type) == name)
		{
			return type;
		}
	}
	return std::nullopt;
}
}	 // namespace

std::string toString(SessionError error)
{
	switch (error)
	{
	case SessionError::Malformed:
		return "the session is not readable";
	case SessionError::UnknownVersion:
		return "the session is from another version";
	}
	return "unknown session error";
}

std::string toJson(const Session& session)
{
	tab_session_json::Session json;
	json.version = sessionVersion;
	json.active_tab = session.activeTab;
	for (const Tab& tab: session.tabs)
	{
		tab_session_json::Tab& jsonTab = json.tabs.emplace_back();
		for (const Entry& entry: tab.history)
		{
			std::vector<tab_session_json::Shown>& jsonEntry = jsonTab.history.emplace_back();
			for (const Shown& shown: entry)
			{
				jsonEntry.push_back(
					{shown.command.empty() ? std::nullopt : std::optional(shown.command), shown.names});
			}
		}
	}
	return glz::write_json(json).value_or("{}");
}

std::expected<Session, SessionError> fromJson(std::string_view json)
{
	tab_session_json::Session parsed;
	if (glz::read<glz::opts {.error_on_unknown_keys = false}>(parsed, json))
	{
		return std::unexpected(SessionError::Malformed);
	}
	if (parsed.version != sessionVersion)
	{
		return std::unexpected(SessionError::UnknownVersion);
	}

	Session session;
	session.activeTab = parsed.active_tab;
	for (const tab_session_json::Tab& jsonTab: parsed.tabs)
	{
		Tab& tab = session.tabs.emplace_back();
		for (const std::vector<tab_session_json::Shown>& jsonEntry: jsonTab.history)
		{
			Entry& entry = tab.history.emplace_back();
			for (const tab_session_json::Shown& shown: jsonEntry)
			{
				entry.push_back({shown.command.value_or(""), shown.names});
			}
		}
	}
	return session;
}

std::optional<Entry> toEntry(const std::vector<SearchMatch>& matches)
{
	Entry entry;
	for (const SearchMatch& match: matches)
	{
		switch (match.searchType)
		{
		case SearchMatch::SearchType::SEARCH_COMMAND:
			if (std::ranges::contains(restorableCommands, match.getCommandType()))
			{
				entry.push_back({SearchMatch::getCommandName(match.getCommandType()), {}});
			}
			break;

		case SearchMatch::SearchType::SEARCH_TOKEN:
			if (!match.tokenNames.empty())
			{
				Shown shown;
				for (const NameHierarchy& name: match.tokenNames)
				{
					shown.names.push_back(NameHierarchy::serialize(name));
				}
				entry.push_back(std::move(shown));
			}
			break;

		default:	// a full-text search, an operator: not shown again
			break;
		}
	}
	if (entry.empty())
	{
		return std::nullopt;
	}
	return entry;
}

std::vector<SearchMatch> toMatches(const Entry& entry, const FindSymbols& find)
{
	std::vector<SearchMatch> matches;
	for (const Shown& shown: entry)
	{
		if (!shown.command.empty())
		{
			if (const std::optional<SearchMatch::CommandType> command = commandNamed(shown.command))
			{
				matches.push_back(SearchMatch::createCommand(*command));
			}
			continue;
		}

		std::vector<NameHierarchy> names;
		names.reserve(shown.names.size());
		for (const std::string& name: shown.names)
		{
			names.push_back(NameHierarchy::deserialize(name));
		}
		for (SearchMatch& match: find(names))
		{
			if (match.isValid() && !match.tokenIds.empty())
			{
				matches.push_back(std::move(match));
			}
		}
	}
	return matches;
}

bool Recorder::record(TabId tab, const std::vector<SearchMatch>& matches)
{
	const std::optional<Entry> entry = toEntry(matches);
	if (!entry)
	{
		return false;
	}

	std::vector<Entry>& history = m_tabs[tab];
	if (!history.empty() && history.back() == *entry)
	{
		return false;
	}
	// Back to what it showed before (Back, or the same symbol again): moved to the end,
	// not kept twice.
	std::erase(history, *entry);
	history.push_back(*entry);
	if (history.size() > maxHistory)
	{
		history.erase(history.begin(), history.end() - static_cast<std::ptrdiff_t>(maxHistory));
	}
	return true;
}

void Recorder::seed(TabId tab, std::vector<Entry> history)
{
	if (history.size() > maxHistory)
	{
		history.erase(history.begin(), history.end() - static_cast<std::ptrdiff_t>(maxHistory));
	}
	m_tabs[tab] = std::move(history);
}

void Recorder::remove(TabId tab)
{
	m_tabs.erase(tab);
	if (m_active == tab)
	{
		m_active.reset();
	}
}

void Recorder::activate(TabId tab)
{
	m_active = tab;
}

void Recorder::clear()
{
	m_tabs.clear();
	m_active.reset();
}

Session Recorder::session() const
{
	Session session;
	for (const auto& [tab, history]: m_tabs)
	{
		if (history.empty())
		{
			continue;
		}
		if (m_active == tab)
		{
			session.activeTab = session.tabs.size();
		}
		session.tabs.push_back({history});
	}
	return session;
}
}	 // namespace tab_session
