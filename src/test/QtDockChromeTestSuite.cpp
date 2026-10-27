#include "Catch2.hpp"

#include "QtDockChrome.h"

#include <QApplication>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include <kddockwidgets/Config.h>
#include <kddockwidgets/KDDockWidgets.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/qtwidgets/views/DockWidget.h>
#include <kddockwidgets/qtwidgets/views/MainWindow.h>

#include <memory>

namespace
{
// A dock whose view has tabs of its own, like the Status view's "Status | Errors". The first
// page has a bar of filters for the header, in a row under its content otherwise.
struct TabbedDock
{
	explicit TabbedDock(const QString& name)
		: dock{new KDDockWidgets::QtWidgets::DockWidget(name)}
		, tabs{new QTabWidget}
		, holder{new QWidget}
		, bar{new QWidget(holder)}
	{
		dock->setTitle(name);
		auto* content{new QWidget};
		auto* layout{new QVBoxLayout(content)};
		layout->addWidget(tabs);

		auto* page{new QWidget};
		auto* pageLayout{new QVBoxLayout(page)};
		pageLayout->addWidget(new QWidget, 1);
		pageLayout->addWidget(holder);
		auto* holderLayout{new QHBoxLayout(holder)};
		holderLayout->addWidget(bar);
		auto* barLayout{new QHBoxLayout(bar)};
		barLayout->setContentsMargins(0, 0, 0, 0);
		barLayout->addWidget(new QCheckBox(QStringLiteral("Info")));
		label = new QLabel(QStringLiteral("5 errors"));
		barLayout->addWidget(label);
		QtDockChrome::offerBarToDockHeader(*page, *bar);

		tabs->addTab(page, QStringLiteral("Status"));
		tabs->addTab(new QWidget, QStringLiteral("Errors"));
		QtDockChrome::offerTabsToDockHeader(*tabs);
		dock->setWidget(content);
	}

	bool barInHeader() const
	{
		return bar->parentWidget() != holder && holder->isHidden();
	}

	bool barAtHome() const
	{
		return bar->parentWidget() == holder && !holder->isHidden();
	}

	KDDockWidgets::QtWidgets::DockWidget* dock;
	QTabWidget* tabs;
	QWidget* holder;
	QWidget* bar;
	QLabel* label{nullptr};
};

QTabBar* headerTabs(const QWidget& window)
{
	for (QTabBar* tabs: window.findChildren<QTabBar*>(QStringLiteral("dock_header_tabs")))
	{
		if (tabs->isVisibleTo(&window))
		{
			return tabs;
		}
	}
	return nullptr;
}
}	 // namespace

TEST_CASE("a dock's header shows the tabs its view offers, and the current page's bar, while it can")
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
	int argc{1};
	char name[]{"Sourcetrail_test"};
	char* argv[]{name, nullptr};
	const QApplication application{argc, argv};

	KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtWidgets);
	auto& config{KDDockWidgets::Config::self()};
	config.setViewFactory(new QtDockViewFactory());
	config.setFlags(
		config.flags() | KDDockWidgets::Config::Flag_HideTitleBarWhenTabsVisible |
		KDDockWidgets::Config::Flag_ShowButtonsOnTabBarIfTitleBarHidden);

	{
		const auto window{std::make_unique<KDDockWidgets::QtWidgets::MainWindow>(QStringLiteral("DockChromeTest"))};
		window->resize(800, 600);
		window->show();

		TabbedDock status{QStringLiteral("Status")};
		window->addDockWidget(status.dock, KDDockWidgets::Location_OnBottom);
		QApplication::processEvents();

		// The header hosts the tabs: the view hides its own.
		REQUIRE(QtDockChrome::tabsInDockHeader(*status.tabs));
		QTabBar* tabs{headerTabs(*window)};
		REQUIRE(tabs != nullptr);
		REQUIRE(tabs->count() == 2);
		REQUIRE(tabs->tabText(0) == QStringLiteral("Status"));
		REQUIRE(tabs->tabText(1) == QStringLiteral("Errors"));

		// Either side switches the tab of the other.
		tabs->setCurrentIndex(1);
		REQUIRE(status.tabs->currentIndex() == 1);
		status.tabs->setCurrentIndex(0);
		REQUIRE(tabs->currentIndex() == 0);

		// The current page's bar is in the header, beside the tabs; another page's is not.
		REQUIRE(status.barInHeader());
		tabs->setCurrentIndex(1);
		REQUIRE(status.barAtHome());
		tabs->setCurrentIndex(0);
		REQUIRE(status.barInHeader());

		// Grown wider than the header has room for, it goes back to its row; and returns when
		// it fits again.
		status.label->setText(QString(400, QLatin1Char('x')));
		QApplication::processEvents();
		REQUIRE(status.barAtHome());
		status.label->setText(QStringLiteral("5 errors"));
		QApplication::processEvents();
		REQUIRE(status.barInHeader());

		// A renamed tab shows once the view says so.
		status.tabs->setTabText(1, QStringLiteral("Errors (3)"));
		QtDockChrome::dockHeaderTabsChanged(*status.tabs);
		REQUIRE(tabs->tabText(1) == QStringLiteral("Errors (3)"));

		// Tabbed with another dock, the group has no title bar: the view shows its tabs again.
		auto* other{new KDDockWidgets::QtWidgets::DockWidget(QStringLiteral("Other"))};
		other->setWidget(new QWidget);
		status.dock->addDockWidgetAsTab(other);
		QApplication::processEvents();
		REQUIRE_FALSE(QtDockChrome::tabsInDockHeader(*status.tabs));
		REQUIRE(headerTabs(*window) == nullptr);
		REQUIRE(status.barAtHome());

		// Alone again, the header has them back.
		other->dockWidget()->close();
		QApplication::processEvents();
		REQUIRE(QtDockChrome::tabsInDockHeader(*status.tabs));
		REQUIRE(headerTabs(*window) != nullptr);
		REQUIRE(status.barInHeader());

		delete other;
	}
	QApplication::processEvents();
}
