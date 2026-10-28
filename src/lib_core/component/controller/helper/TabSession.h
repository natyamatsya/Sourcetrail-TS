#ifndef TAB_SESSION_H
#define TAB_SESSION_H

#include <cstddef>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "NameHierarchy.h"
#include "SearchMatch.h"
#include "TabIds.h"

// What a project's tabs showed, to show again when the project is opened next: per tab
// what it showed last and the activations before it, and which tab was in front.
// Symbols are kept by their serialized names, so the session outlives a reindex; what
// is gone by then is left out when it is restored.
namespace tab_session
{
// One thing an activation showed: a command (the overview, the errors, the legend, the
// boundaries) by its name, or symbols by their serialized names.
struct Shown
{
	std::string command;
	std::vector<std::string> names;

	bool operator==(const Shown& other) const = default;
};

// What one activation showed.
using Entry = std::vector<Shown>;

struct Tab
{
	std::vector<Entry> history;	// oldest first; the last is what the tab shows
};

struct Session
{
	std::vector<Tab> tabs;
	std::size_t activeTab = 0;
};

enum class SessionError
{
	Malformed,
	UnknownVersion
};
std::string toString(SessionError error);

// How many activations a tab keeps (the one it shows among them).
inline constexpr std::size_t maxHistory = 20;

std::string toJson(const Session& session);
std::expected<Session, SessionError> fromJson(std::string_view json);

// What `matches` show, as the session keeps it; nothing for what cannot be shown again
// (a full-text search, a node-type filter, no match).
std::optional<Entry> toEntry(const std::vector<SearchMatch>& matches);

// The matches that show `entry` again: commands as they are, symbols by `find`, which
// gives the matches for the names it still knows. Empty when nothing is left.
using FindSymbols = std::function<std::vector<SearchMatch>(const std::vector<NameHierarchy>& names)>;
std::vector<SearchMatch> toMatches(const Entry& entry, const FindSymbols& find);

// The tabs' activations as they happen (TabsController, from each tab's MessageTabState).
class Recorder
{
public:
	// The tab showed `matches`. Returns whether the session changed.
	bool record(TabId tab, const std::vector<SearchMatch>& matches);
	// A restored tab starts with the activations before the one it shows.
	void seed(TabId tab, std::vector<Entry> history);
	void remove(TabId tab);
	void activate(TabId tab);
	void clear();

	// The tabs in the order they were opened; those that showed nothing to keep are left out.
	Session session() const;

private:
	std::map<TabId, std::vector<Entry>> m_tabs;
	std::optional<TabId> m_active;
};
}	 // namespace tab_session

#endif	  // TAB_SESSION_H
