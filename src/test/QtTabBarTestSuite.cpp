#include "Catch2.hpp"

#include "QtActions.h"
#include "QtTabBar.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QKeySequence>
#include <QMenu>
#include <QTimer>

#include <array>
#include <functional>
#include <vector>

namespace
{
struct OffscreenPlatform
{
	OffscreenPlatform()
	{
		qputenv("QT_QPA_PLATFORM", "offscreen");
	}
};

class OffscreenApplication: private OffscreenPlatform
{
private:
	int m_argc{1};
	char m_name[17]{"Sourcetrail_test"};
	std::array<char*, 2> m_argv{m_name, nullptr};
	QApplication m_application{m_argc, m_argv.data()};
};

// Opens the tab bar's context menu on tab `index`, as a right click does; while it is open, runs
// `meanwhile` and then the item whose text starts with `item`.
void pickFromMenu(QtTabBar& bar, int index, const std::function<void()>& meanwhile, const QString& item)
{
	QTimer::singleShot(0, [&]() {
		meanwhile();
		QMenu* const menu{qobject_cast<QMenu*>(QApplication::activePopupWidget())};
		CHECK(menu != nullptr);	  // no REQUIRE: this runs inside the menu's event loop
		if (menu == nullptr)
		{
			return;
		}
		for (QAction* const action: menu->actions())
		{
			if (action->text().startsWith(item))
			{
				action->trigger();
			}
		}
		menu->close();
	});
	const QPoint at{bar.tabRect(index).center()};
	QContextMenuEvent event{QContextMenuEvent::Mouse, at, bar.mapToGlobal(at)};
	QApplication::sendEvent(&bar, &event);	  // returns once the menu has closed
}
}	 // namespace

TEST_CASE("a tab's context menu acts on the tab it was opened on")
{
	const OffscreenApplication application;
	QtTabBar bar;
	for (int i = 0; i < 4; i++)
	{
		bar.addTab(QStringLiteral("Tab %1").arg(i));
		bar.setTabData(i, i + 100);	  // what QtTabsView keeps there: the tab's id
	}
	bar.resize(1200, 30);
	bar.show();
	QApplication::processEvents();

	std::vector<int> closed;
	std::vector<int> closedRightOf;
	QObject::connect(&bar, &QtTabBar::closeTab, [&](int index) { closed.push_back(index); });
	QObject::connect(&bar, &QtTabBar::closeTabsToRight, [&](int index) { closedRightOf.push_back(index); });

	SECTION("tabs shift while the menu is open")
	{
		// Opened on the third tab; the first closes meanwhile: the third is the second now.
		pickFromMenu(bar, 2, [&]() { bar.removeTab(0); }, QStringLiteral("Close Tab"));
		REQUIRE(closed == std::vector<int> {1});
		REQUIRE(bar.tabData(1).toInt() == 102);

		pickFromMenu(bar, 1, [&]() { bar.removeTab(0); }, QStringLiteral("Close tabs to the right"));
		REQUIRE(closedRightOf == std::vector<int> {0});
	}

	SECTION("the tab closes while the menu is open")
	{
		pickFromMenu(bar, 1, [&]() { bar.removeTab(1); }, QStringLiteral("Close Tab"));
		REQUIRE(closed.empty());
	}

	SECTION("the menu's actions go with it")
	{
		for (int i = 0; i < 3; i++)
		{
			pickFromMenu(bar, 0, []() {}, QStringLiteral("nothing"));
		}
		REQUIRE(bar.findChildren<QAction*>().isEmpty());
	}
}

TEST_CASE("close tab lists each of its shortcuts once")
{
	const OffscreenApplication application;
	const QString native{QKeySequence(Qt::CTRL | Qt::Key_W).toString(QKeySequence::NativeText)};
	const QString f4{QKeySequence(Qt::CTRL | Qt::Key_F4).toString(QKeySequence::NativeText)};
	REQUIRE(QtActions::closeTab().menuText() == QStringLiteral("Close Tab\t%1 | %2 | Middle Click").arg(native, f4));
}
