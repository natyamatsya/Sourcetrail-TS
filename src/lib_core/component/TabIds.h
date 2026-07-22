#ifndef TAB_IDS_H
#define TAB_IDS_H

#include <QMetaType>

enum class TabId : unsigned
{
	NONE = 0,
	
	APP = 1,
	BACKGROUND = 2,
	IGNORE = 3,
	
	USER = 10
};

// Guarded out of module purviews -- see the note on Q_DECLARE_METATYPE(Id) in utility/Id.h
// (the macro's QMetaType specialization cannot cross an `import`; MSVC C1116 on re-import).
#ifndef SRCTRL_MODULE_PURVIEW
Q_DECLARE_METATYPE(TabId)
#endif

class TabIds
{
public:
	static TabId app();
	static TabId background();
	static TabId ignore();

	static TabId nextTab();
	static TabId currentTab();

	static void setCurrentTabId(TabId currentTabId);

private:
	static TabId s_nextTabId;
	static TabId s_currentTabId;
};

#endif
