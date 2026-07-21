#ifndef MESSAGE_TABS_CHANGED_H
#define MESSAGE_TABS_CHANGED_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>
#include <vector>

#include "TabIds.h"
#endif

// Broadcast by TabsController whenever the set of tabs, their titles, or the active
// tab changes. Carries the full snapshot so any observer (e.g. AgentControlController)
// can cache authoritative tab state without reaching into the GUI component tree.
SRCTRL_EXPORT class MessageTabsChanged: public Message<MessageTabsChanged>
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
