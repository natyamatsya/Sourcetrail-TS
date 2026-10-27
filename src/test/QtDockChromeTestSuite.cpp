#include "Catch2.hpp"

#include "QtDockChrome.h"

#include <QApplication>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include <kddockwidgets/Config.h>
#include <kddockwidgets/KDDockWidgets.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/core/FloatingWindow.h>
#include <kddockwidgets/core/TitleBar.h>
#include <kddockwidgets/qtcommon/View.h>
#include <kddockwidgets/qtwidgets/views/DockWidget.h>
#include <kddockwidgets/qtwidgets/views/MainWindow.h>

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

// An offscreen application with KDDockWidgets, which builds its docks from the chrome.
class DockingApplication: private OffscreenPlatform
{
public:
	DockingApplication()
	{
		KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtWidgets);
		auto& config{KDDockWidgets::Config::self()};
		config.setViewFactory(new QtDockViewFactory());
		config.setFlags(
			config.flags() | KDDockWidgets::Config::Flag_HideTitleBarWhenTabsVisible |
			KDDockWidgets::Config::Flag_ShowButtonsOnTabBarIfTitleBarHidden);
	}

private:
	int m_argc{1};
	char m_name[17]{"Sourcetrail_test"};
	std::array<char*, 2> m_argv{m_name, nullptr};
	QApplication m_application{m_argc, m_argv.data()};
};

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
	const DockingApplication application;
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

namespace
{
KDDockWidgets::QtWidgets::DockWidget* plainDock(const QString& name)
{
	auto* dock{new KDDockWidgets::QtWidgets::DockWidget(name)};
	dock->setTitle(name);
	dock->setWidget(new QWidget);
	return dock;
}

QtDockFloatingWindow* floatingWindowOf(const KDDockWidgets::QtWidgets::DockWidget& dock)
{
	const KDDockWidgets::Core::FloatingWindow* window{dock.dockWidget()->floatingWindow()};
	if (window == nullptr)
	{
		return nullptr;
	}
	return qobject_cast<QtDockFloatingWindow*>(KDDockWidgets::QtCommon::View_qt::asQWidget(window->view()));
}

bool headerRowOnCanvas(const QtDockFloatingWindow& window)
{
	const QWidget* header{KDDockWidgets::QtCommon::View_qt::asQWidget(window.floatingWindow()->titleBar()->view())};
	return header->property("dockFloatingSeveralCards").toBool();
}
}	 // namespace

TEST_CASE("a floating window frames its cards with rounded corners")
{
	const DockingApplication application;
	{
		const auto mainWindow{std::make_unique<KDDockWidgets::QtWidgets::MainWindow>(QStringLiteral("FloatingTest"))};
		mainWindow->resize(800, 600);
		mainWindow->show();

		auto* graph{plainDock(QStringLiteral("Graph"))};
		auto* code{plainDock(QStringLiteral("Code"))};
		mainWindow->addDockWidget(graph, KDDockWidgets::Location_OnLeft);
		mainWindow->addDockWidget(code, KDDockWidgets::Location_OnRight);
		QApplication::processEvents();

		graph->setFloating(true);
		QApplication::processEvents();
		QtDockFloatingWindow* window{floatingWindowOf(*graph)};
		REQUIRE(window != nullptr);
		window->resize(400, 300);
		QApplication::processEvents();

		// A single card fills the window's frame (see-through), or sits on its canvas (opaque),
		// and leaves the margin to resize the window by to the window.
		const int margin{window->resizeMargin()};
		REQUIRE(margin >= 4);
		REQUIRE_FALSE(window->holdsSeveralCards());
		REQUIRE_FALSE(headerRowOnCanvas(*window));
		REQUIRE(window->testAttribute(Qt::WA_TranslucentBackground) == window->isTranslucent());
		const QMargins single{window->layout()->contentsMargins()};
		REQUIRE(single.left() >= margin);
		REQUIRE(single.top() >= margin);
		REQUIRE(single.right() >= margin);
		REQUIRE(single.bottom() >= margin);
		REQUIRE(window->cardRect() == window->rect().marginsRemoved(single));
		if (window->isTranslucent())
		{
			REQUIRE(window->cardRect() == window->frameRect());
		}
#ifdef Q_OS_MACOS
		// Unless turned off, as KDDockWidgets' own translucency is.
		REQUIRE(window->isTranslucent() == !qEnvironmentVariableIsSet("KDDW_NO_TRANSLUCENCY"));
#endif

		{
			const QImage picture{window->grab().toImage()};
			const QRect card{window->cardRect()};
			if (window->isTranslucent())
			{
				// The corners show what is behind the window.
				REQUIRE(qAlpha(picture.pixel(0, 0)) < 16);
				REQUIRE(qAlpha(picture.pixel(card.topLeft())) < 16);
				REQUIRE(qAlpha(picture.pixel(card.bottomRight())) < 16);
			}
			else
			{
				REQUIRE(QColor{picture.pixel(card.topLeft())} == window->canvasColor());
			}
			// The hairline border along the card's edges.
			const QColor border{window->isTranslucent() ? window->borderColor() : window->cardBorderColor()};
			REQUIRE(QColor{picture.pixel(card.center().x(), card.bottom())} == border);
			REQUIRE(QColor{picture.pixel(card.left(), card.center().y())} == border);
		}

		{
			// The margin along the edges still resizes the window: drag its left edge outwards.
			const QSize before{window->size()};
			const QPoint edge{1, window->height() / 2};
			const QPoint outwards{edge - QPoint{30, 0}};
			QMouseEvent press{
				QEvent::MouseButtonPress, QPointF{edge}, QPointF{window->mapToGlobal(edge)}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
			QApplication::sendEvent(window, &press);
			QMouseEvent move{
				QEvent::MouseMove, QPointF{outwards}, QPointF{window->mapToGlobal(outwards)}, Qt::NoButton, Qt::LeftButton, Qt::NoModifier};
			QApplication::sendEvent(window, &move);
			QMouseEvent release{
				QEvent::MouseButtonRelease, QPointF{outwards}, QPointF{window->mapToGlobal(outwards)}, Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
			QApplication::sendEvent(window, &release);
			QApplication::processEvents();
			REQUIRE(window->width() > before.width());
			REQUIRE(window->height() == before.height());
		}

		// Two cards side by side: the canvas around them, the window's header row on it.
		graph->dockWidget()->addDockWidgetToContainingWindow(code->dockWidget(), KDDockWidgets::Location_OnRight);
		QApplication::processEvents();
		REQUIRE(floatingWindowOf(*code) == window);
		REQUIRE(window->holdsSeveralCards());
		REQUIRE(headerRowOnCanvas(*window));
		REQUIRE(window->cardRect().isEmpty());
		const QMargins several{window->layout()->contentsMargins()};
		REQUIRE(several.left() >= margin + (window->isTranslucent() ? QtDockChrome::gutterWidth : 0));
		REQUIRE(several.top() == margin);

		// Back to one card.
		code->dockWidget()->close();
		QApplication::processEvents();
		REQUIRE_FALSE(window->holdsSeveralCards());
		REQUIRE_FALSE(headerRowOnCanvas(*window));
		REQUIRE(window->layout()->contentsMargins() == single);

		// Docks tabbed together are a single card.
		graph->addDockWidgetAsTab(code);
		QApplication::processEvents();
		REQUIRE(floatingWindowOf(*code) == window);
		REQUIRE_FALSE(window->holdsSeveralCards());

		// Docked again, alone, it leaves no floating window.
		code->dockWidget()->close();
		QApplication::processEvents();
		REQUIRE(graph->dockWidget()->setFloating(false));
		QApplication::processEvents();
		REQUIRE(graph->dockWidget()->isInMainWindow());
		REQUIRE(floatingWindowOf(*graph) == nullptr);
	}
	QApplication::processEvents();
}
