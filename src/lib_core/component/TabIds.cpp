#include "TabIds.h"

#include <type_traits>

using namespace std;

TabId TabIds::s_nextTabId = TabId::USER;
TabId TabIds::s_currentTabId = TabId::NONE;

namespace
{

TabId operator ++(TabId &tabId, int)
{
	return static_cast<TabId>(reinterpret_cast<underlying_type_t<TabId> &>(tabId)++);
}

}

TabId TabIds::app()
{
	return TabId::APP;
}

TabId TabIds::background()
{
	return TabId::BACKGROUND;
}

TabId TabIds::ignore()
{
	return TabId::IGNORE;
}

TabId TabIds::nextTab()
{
	return s_nextTabId++;
}

TabId TabIds::currentTab()
{
	return s_currentTabId;
}

void TabIds::setCurrentTabId(TabId currentTabId)
{
	s_currentTabId = currentTabId;
}



