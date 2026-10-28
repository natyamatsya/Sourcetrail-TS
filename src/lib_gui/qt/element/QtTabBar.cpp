#include "QtTabBar.h"

#include "QtContextMenu.h"
#include "logging.h"
#include "QtActions.h"

#include <QAction>
#include <QContextMenuEvent>


QtTabBar::QtTabBar(QWidget* parent)
	: QTabBar(parent)
{
	setFocusPolicy(Qt::NoFocus);
}

QSize QtTabBar::minimumSizeHint() const
{
	return QSize(0, QTabBar::minimumSizeHint().height());
}

QSize QtTabBar::tabSizeHint(int index) const
{
	return QSize(300, QTabBar::tabSizeHint(index).height());
}

QSize QtTabBar::minimumTabSizeHint(int index) const
{
	return QSize(45, QTabBar::minimumTabSizeHint(index).height());
}

void QtTabBar::contextMenuEvent(QContextMenuEvent* event)
{
	// The tab is the one under the pointer when the menu opens. Tabs can close or move while
	// it is open (an activation, an indexing run reloading them), and the index then names
	// another one; the tab's data (its TabId) follows it.
	const int tabIndex = tabAt(event->pos());
	if (tabIndex == -1)
		return;
	const QVariant tab = tabData(tabIndex);

	QtContextMenu menu(event, this);

	// Children of the menu: they go when it closes.
	QAction* closeTabAction = new QAction(QtActions::closeTab().menuText(), &menu);
	menu.addAction(closeTabAction);
	connect(closeTabAction, &QAction::triggered, this, [this, tab, tabIndex]()
	{
		if (const int index = indexOfTab(tab, tabIndex); index != -1)
			emit closeTab(index);
	});

	QAction* closeTabsToRightAction = new QAction(tr("Close tabs to the right"), &menu);
	menu.addAction(closeTabsToRightAction);
	connect(closeTabsToRightAction, &QAction::triggered, this, [this, tab, tabIndex]()
	{
		// Right of the tab the menu was opened on, not of the current one.
		if (const int index = indexOfTab(tab, tabIndex); index != -1)
		{
			LOG_INFO("Handling closeTabs... emitting signal to close tabs right of tab nr. " + std::to_string(index));
			emit closeTabsToRight(index);
		}
	});

	menu.show();
}

int QtTabBar::indexOfTab(const QVariant& tab, int index) const
{
	if (!tab.isValid())
		return index < count() ? index : -1;	// a tab without data: only its index tells
	for (int i = 0; i < count(); i++)
	{
		if (tabData(i) == tab)
			return i;
	}
	return -1;	// closed meanwhile
}

void QtTabBar::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::MiddleButton)
	{
		if (int tabIdx = tabAt(event->pos()); tabIdx != -1)
			emit closeTab(tabIdx);
	}
	QTabBar::mouseReleaseEvent(event);
}
