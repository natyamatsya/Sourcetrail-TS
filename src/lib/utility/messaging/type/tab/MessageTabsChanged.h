#ifndef MESSAGE_TABS_CHANGED_H
#define MESSAGE_TABS_CHANGED_H

#include <string>
#include <vector>

#include "Message.h"
#include "TabIds.h"

// Broadcast by TabsController whenever the set of tabs, their titles, or the active
// tab changes. Carries the full snapshot so any observer (e.g. AgentControlController)
// can cache authoritative tab state without reaching into the GUI component tree.
class MessageTabsChanged: public Message<MessageTabsChanged>
{
public:
	struct TabInfo
	{
		TabId tabId;
		std::string title;
		bool active;
	};

	MessageTabsChanged(std::vector<TabInfo> tabs): tabs(std::move(tabs))
	{
		setSchedulerId(TabIds::app());
	}

	static const std::string getStaticType()
	{
		return "MessageTabsChanged";
	}

	const std::vector<TabInfo> tabs;
};

#endif	  // MESSAGE_TABS_CHANGED_H
