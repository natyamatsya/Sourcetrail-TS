#include "Catch2.hpp"

#include "QtContextMenu.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QWidget>

#include <array>
#include <memory>

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
}	 // namespace

TEST_CASE("a context menu outlives the widget it was opened on")
{
	// What runs while a menu is open (an activation rebuilding the code view) can delete the
	// widget it was opened on, and with it the widget's children: the menu is one. A menu kept
	// in the QtContextMenu on the caller's stack was then freed by its parent, too.
	const OffscreenApplication application;
	auto origin{std::make_unique<QWidget>()};
	QContextMenuEvent event{QContextMenuEvent::Mouse, QPoint{1, 1}, QPoint{1, 1}};
	{
		QtContextMenu menu(&event, origin.get());
		QAction action{QStringLiteral("Something")};
		menu.addAction(&action);

		origin.reset();	   // as deleting the code area while its menu is open does

		menu.addAction(&action);	// nothing left to add it to
		menu.addSeparator();
	}	 // and destroying the QtContextMenu frees nothing twice
	SUCCEED();
}
