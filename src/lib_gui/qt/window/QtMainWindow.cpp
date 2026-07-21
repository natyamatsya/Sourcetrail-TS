#include "QtMainWindow.h"

#include "Application.h"
#ifndef SRCTRL_MODULE_BUILD
#include "ApplicationSettings.h"
#include "Bookmark.h"
#endif
#include "CompositeView.h"
#ifndef SRCTRL_MODULE_BUILD
#include "FileSystem.h"
#include "MessageActivateBase.h"
#include "MessageActivateLegend.h"
#include "MessageActivateOverview.h"
#include "MessageBookmarkActivate.h"
#include "MessageBookmarkBrowse.h"
#include "MessageBookmarkCreate.h"
#include "MessageCloseProject.h"
#include "MessageCodeReference.h"
#include "MessageCustomTrailShow.h"
#include "MessageErrorsHelpMessage.h"
#include "MessageFind.h"
#include "MessageFocusView.h"
#include "MessageHistoryRedo.h"
#include "MessageHistoryUndo.h"
#include "MessageIndexingInterrupted.h"
#include "MessageIndexingShowDialog.h"
#include "MessageLoadProject.h"
#include "MessageRefresh.h"
#include "MessageRefreshUI.h"
#include "MessageResetZoom.h"
#include "MessageSaveAsImage.h"
#include "MessageStatus.h"
#include "MessageTabClose.h"
#include "MessageTabOpen.h"
#include "MessageTabSelect.h"
#include "MessageWindowClosed.h"
#include "MessageZoom.h"
#endif
#include "QtAbout.h"
#include "QtActions.h"
#include "QtBuildJsonBrowser.h"
#include "QtContextMenu.h"
#include "QtFileDialog.h"
#include "QtKeyboardShortcuts.h"
#include "QtLicenseWindow.h"
#include "QtPreferencesWindow.h"
#include "QtProjectWizard.h"
#include "QtResources.h"
#include "QtStartScreen.h"
#include "QtViewWidgetWrapper.h"
#ifndef SRCTRL_MODULE_BUILD
#include "ResourcePaths.h"
#endif
#include "TabbedView.h"
#ifndef SRCTRL_MODULE_BUILD
#include "UserPaths.h"
#endif
#include "View.h"
#include "logging.h"
#include "tracing.h"
#ifndef SRCTRL_MODULE_BUILD
#include "utilityApp.h"
#endif
#include "utilityQt.h"
#ifndef SRCTRL_MODULE_BUILD
#include "utilityString.h"
#endif

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QMenuBar>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QToolBar>
#include <QToolTip>
#include <QVBoxLayout>

#include <kddockwidgets/Config.h>
#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/KDDockWidgets.h>
#include <kddockwidgets/LayoutSaver.h>

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.data;
import srctrl.file;
import srctrl.messaging;
import srctrl.process;
import srctrl.settings;
import srctrl.utility;
#endif


using namespace utility;

QtViewToggle::QtViewToggle(View* view, QWidget* parent): QWidget(parent), m_view(view) {}

void QtViewToggle::clear()
{
	m_view = nullptr;
}

void QtViewToggle::toggledByAction()
{
	if (m_view)
	{
		dynamic_cast<QtMainWindow*>(parent())->toggleView(m_view, true);
	}
}

void QtViewToggle::toggledByUI()
{
	if (m_view)
	{
		dynamic_cast<QtMainWindow*>(parent())->toggleView(m_view, false);
	}
}


MouseReleaseFilter::MouseReleaseFilter(QObject* parent): QObject(parent)
{
	m_backButton = ApplicationSettings::getInstance()->getControlsMouseBackButton();
	m_forwardButton = ApplicationSettings::getInstance()->getControlsMouseForwardButton();
}

bool MouseReleaseFilter::eventFilter(QObject* obj, QEvent* event)
{
	if (event->type() == QEvent::MouseButtonRelease)
	{
		QMouseEvent* mouseEvent = dynamic_cast<QMouseEvent*>(event);

		if (mouseEvent->button() == m_backButton)
		{
			MessageHistoryUndo().dispatch();
			return true;
		}
		else if (mouseEvent->button() == m_forwardButton)
		{
			MessageHistoryRedo().dispatch();
			return true;
		}
	}

	return QObject::eventFilter(obj, event);
}

QtMainWindow::QtMainWindow()
	: Super(QStringLiteral("SourcetrailMainWindow"))
	, m_windowStack(this)
{
	setObjectName(QStringLiteral("QtMainWindow"));
	// KDDockWidgets owns the central layout area — no setCentralWidget/
	// setDockNestingEnabled here (those are QDockWidget-era APIs KDDW replaces).

	setWindowIcon(QIcon(QString::fromUtf8(QtResources::ICON_LOGO_1024_1024)));
	setWindowFlags(Qt::Widget);

	QApplication *app = dynamic_cast<QApplication *>(QCoreApplication::instance());
	app->installEventFilter(new MouseReleaseFilter(this));

	refreshStyle();

	if constexpr (!utility::Platform::isMac())
	{
		// can only be done once, because resetting the style on the QCoreApplication causes crash
		app->setStyleSheet(QtResources::loadStyleSheet(QtResources::MAIN_SCROLLBAR_CSS));
	}

	setupProjectMenu();
	setupEditMenu();
	setupViewMenu();
	setupHistoryMenu();
	setupBookmarksMenu();
	setupHelpMenu();

	// Need to call loadLayout here for right DockWidget size on Linux
	// Second call is in Application.cpp
	loadLayout();
}

QtMainWindow::~QtMainWindow()
{
	for (DockWidget& dockWidget: m_dockWidgets)
	{
		dockWidget.toggle->clear();
	}
}

void QtMainWindow::addView(View* view)
{
	const QString name = QString::fromStdString(view->getName());
	if (name == QLatin1String("Tabs"))
	{
		QToolBar* toolBar = new QToolBar();
		toolBar->setObjectName("Tool" + name);
		toolBar->setMovable(false);
		toolBar->setFloatable(false);
		toolBar->setStyleSheet(QStringLiteral("* { margin: 0; }"));
		toolBar->addWidget(QtViewWidgetWrapper::getWidgetOfView(view));
		addToolBar(toolBar);
		return;
	}

	// The view name is unique and stable, so it doubles as the dock's uniqueName
	// (the id LayoutSaver persists) and its user-visible title.
	auto* dock = new KDDockWidgets::QtWidgets::DockWidget(name);
	dock->setTitle(name);

	// Host the view's widget inside a stable container that IS the dock's guest, and
	// swap the inner widget in overrideView(). Setting the view's widget directly as
	// the guest would leave the dock with a dangling pointer when the view replaces
	// its widget (e.g. on a tab change), crashing KDDW's DockWidget::widget().
	QWidget* container = new QWidget();
	QVBoxLayout* layout = new QVBoxLayout(container);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(QtViewWidgetWrapper::getWidgetOfView(view));
	dock->setWidget(container);

	// New docks tile to the right of the current layout; the persisted KDDW layout
	// (LayoutSaver, see loadDockWidgetLayout) overrides this on later launches.
	addDockWidget(dock, KDDockWidgets::Location_OnRight);

	QtViewToggle* toggle = new QtViewToggle(view, this);
	connect(
		dock,
		&KDDockWidgets::QtWidgets::DockWidget::isOpenChanged,
		toggle,
		&QtViewToggle::toggledByUI);

	QAction* action = new QAction(name + " Window", this);
	action->setCheckable(true);
	connect(action, &QAction::triggered, toggle, &QtViewToggle::toggledByAction);
	m_viewMenu->insertAction(m_viewSeparator, action);

	DockWidget dockWidget;
	dockWidget.widget = dock;
	dockWidget.view = view;
	dockWidget.action = action;
	dockWidget.toggle = toggle;

	m_dockWidgets.push_back(dockWidget);
}

void QtMainWindow::overrideView(View* view)
{
	const QString name = QString::fromStdString(view->getName());
	if (name == QLatin1String("Tabs"))
	{
		return;
	}

	// Docks are keyed by their view's (unique) name, so we look them up directly —
	// no more matching on the dock title (which the old code had to guard against a
	// stray '&' accelerator prefix).
	KDDockWidgets::QtWidgets::DockWidget* dock = nullptr;
	for (const DockWidget& dockWidget: m_dockWidgets)
	{
		if (QString::fromStdString(dockWidget.view->getName()) == name)
		{
			dock = dockWidget.widget;
			break;
		}
	}

	if (!dock)
	{
		LOG_ERROR_STREAM(<< "Couldn't find view to override: " << name.toStdString());
		return;
	}

	// Swap the inner widget inside the dock's stable container (see addView), never
	// the dock guest itself — so KDDW never dereferences a widget the view may have
	// destroyed. dock->widget() is the container we own, so it is always valid.
	QWidget* container = dock->widget();
	QLayout* layout = container ? container->layout() : nullptr;
	if (!layout)
	{
		return;
	}

	QLayoutItem* item = layout->itemAt(0);
	QWidget* oldWidget = item ? item->widget() : nullptr;
	QWidget* newWidget = QtViewWidgetWrapper::getWidgetOfView(view);
	if (oldWidget == newWidget)
	{
		return;
	}

	if (oldWidget)
	{
		layout->removeWidget(oldWidget);
		oldWidget->hide();
	}
	if (newWidget)
	{
		layout->addWidget(newWidget);
		newWidget->show();
	}
}

void QtMainWindow::removeView(View* view)
{
	for (size_t i = 0; i < m_dockWidgets.size(); i++)
	{
		if (m_dockWidgets[i].view == view)
		{
			KDDockWidgets::QtWidgets::DockWidget* dock = m_dockWidgets[i].widget;
			// Reparent the view's own widget out of the dock's container first, so
			// disposing the dock (which owns the container) doesn't also delete it —
			// the View manages its widget's lifetime.
			if (QWidget* container = dock->widget())
			{
				if (QLayout* layout = container->layout())
				{
					if (QLayoutItem* item = layout->itemAt(0))
					{
						if (QWidget* inner = item->widget())
						{
							layout->removeWidget(inner);
							inner->setParent(nullptr);
						}
					}
				}
			}
			dock->close();
			dock->deleteLater();
			m_dockWidgets.erase(m_dockWidgets.begin() + i);
			return;
		}
	}
}

void QtMainWindow::showView(View* view)
{
	getDockWidgetForView(view)->widget->open();
}

void QtMainWindow::hideView(View* view)
{
	getDockWidgetForView(view)->widget->close();
}

View* QtMainWindow::findFloatingView(const std::string& name) const
{
	for (size_t i = 0; i < m_dockWidgets.size(); i++)
	{
		if (std::string(m_dockWidgets[i].view->getName()) == name &&
			m_dockWidgets[i].widget->isFloating())
		{
			return m_dockWidgets[i].view;
		}
	}

	return nullptr;
}

void QtMainWindow::loadLayout()
{
	QSettings settings(
		QString::fromStdString(UserPaths::getWindowSettingsFilePath().str()), QSettings::IniFormat);

	settings.beginGroup(QStringLiteral("MainWindow"));
	resize(settings.value(QStringLiteral("size"), QSize(600, 400)).toSize());
	move(settings.value(QStringLiteral("position"), QPoint(200, 200)).toPoint());
	if (settings.value(QStringLiteral("maximized"), false).toBool())
	{
		showMaximized();
	}
	// Default OFF → thin tab-strip drag handles (see setShowDockWidgetTitleBars).
	setShowDockWidgetTitleBars(settings.value(QStringLiteral("showTitleBars"), false).toBool());
	settings.endGroup();
	loadDockWidgetLayout();

	// Both the MainWindow group above and the KDDW LayoutSaver restore in
	// loadDockWidgetLayout() can bring back a degenerate main-window state — a
	// minimized/off-screen/tiny geometry saved by a headless run or left after a
	// monitor change — which would leave the window invisible. Repair it.
	sanitizeWindowGeometry();
}

void QtMainWindow::sanitizeWindowGeometry()
{
	// A restored Qt::WindowMinimized main window never becomes visible on show().
	if (windowState() & Qt::WindowMinimized)
	{
		setWindowState(windowState() & ~Qt::WindowMinimized);
	}

	if (isMaximized())
	{
		return;	// maximized is inherently on-screen and correctly sized
	}

	// Clamp a degenerately small window (e.g. the 82x96 an offscreen run persists)
	// up to a usable default.
	if (width() < 400 || height() < 300)
	{
		resize(1000, 700);
	}

	// Ensure the window still intersects an available screen; if a saved position
	// is now off every display (unplugged monitor, resolution change), re-center it
	// on the primary screen.
	const QRect frame = frameGeometry();
	bool onScreen = false;
	for (const QScreen* screen: QGuiApplication::screens())
	{
		if (screen->availableGeometry().intersects(frame))
		{
			onScreen = true;
			break;
		}
	}
	if (!onScreen)
	{
		if (QScreen* primary = QGuiApplication::primaryScreen())
		{
			const QRect avail = primary->availableGeometry();
			move(avail.center() - QPoint(width() / 2, height() / 2));
		}
	}
}

void QtMainWindow::loadDockWidgetLayout()
{
	QSettings settings(
		QString::fromStdString(UserPaths::getWindowSettingsFilePath().str()), QSettings::IniFormat);

	// New key: KDDockWidgets serializes as JSON via LayoutSaver, which also restores
	// floating windows and clamps their geometry to the available screens. The legacy
	// QMainWindow::saveState() blob under "DOCK_LOCATIONS" is a different, incompatible
	// format, so a fresh key means old settings are simply ignored (default layout).
	const QByteArray layout = settings.value(QStringLiteral("DOCK_LAYOUT_KDDW")).toByteArray();
	bool restored = false;
	if (!layout.isEmpty())
	{
		KDDockWidgets::LayoutSaver saver;
		restored = saver.restoreLayout(layout);
		if (!restored)
		{
			LOG_WARNING("Saved dock layout could not be restored; using the default arrangement.");
		}
	}

	// No usable saved layout (first run, or a failed/incompatible restore) → build the
	// classic default arrangement rather than leaving the addView() fallback tiling.
	// (On the constructor's first loadLayout() call m_dockWidgets is still empty, so
	// this no-ops there and runs for real on the post-createInstance() call.)
	if (!restored && !m_dockWidgets.empty())
	{
		applyDefaultDockLayout();
	}

	for (const DockWidget& dock: m_dockWidgets)
	{
		dock.action->setChecked(dock.widget->isOpen());
	}
}

void QtMainWindow::loadWindow(bool showStartWindow)
{
	if (showStartWindow)
	{
		showStartScreen();
	}
}

void QtMainWindow::saveLayout()
{
	// Never persist layout from an offscreen/headless run: it captures a degenerate
	// geometry (minimized, near-zero size) that would leave a later native launch
	// invisible. Headless indexing/verification runs must not poison the real config.
	if (QGuiApplication::platformName() == QLatin1String("offscreen"))
	{
		return;
	}

	QSettings settings(
		QString::fromStdString(UserPaths::getWindowSettingsFilePath().str()), QSettings::IniFormat);

	settings.beginGroup(QStringLiteral("MainWindow"));
	settings.setValue(QStringLiteral("maximized"), isMaximized());
	if (!isMaximized())
	{
		settings.setValue(QStringLiteral("size"), size());
		settings.setValue(QStringLiteral("position"), pos());
	}
	settings.setValue(QStringLiteral("showTitleBars"), m_showDockWidgetTitleBars);
	settings.endGroup();

	KDDockWidgets::LayoutSaver saver;
	settings.setValue(QStringLiteral("DOCK_LAYOUT_KDDW"), saver.serializeLayout());
}

void QtMainWindow::updateHistoryMenu(std::shared_ptr<MessageBase> message)
{
	const size_t historyMenuSize = 20;

	if (message && dynamic_cast<MessageActivateBase*>(message.get()))
	{
		std::vector<SearchMatch> matches =
			dynamic_cast<MessageActivateBase*>(message.get())->getSearchMatches();
		if (matches.size() && !matches[0].text.empty())
		{
			std::vector<std::shared_ptr<MessageBase>> history = {message};
			std::set<SearchMatch> uniqueMatches = {matches[0]};

			for (std::shared_ptr<MessageBase> m: m_history)
			{
				if (uniqueMatches
						.insert(dynamic_cast<MessageActivateBase*>(m.get())->getSearchMatches()[0])
						.second)
				{
					history.push_back(m);

					if (history.size() >= historyMenuSize)
					{
						break;
					}
				}
			}

			m_history = history;
		}
	}

	setupHistoryMenu();
}

void QtMainWindow::clearHistoryMenu()
{
	m_history.clear();
	setupHistoryMenu();
}

void QtMainWindow::updateBookmarksMenu(const std::vector<std::shared_ptr<Bookmark>>& bookmarks)
{
	m_bookmarks = bookmarks;
	setupBookmarksMenu();
}

void QtMainWindow::setContentEnabled(bool enabled)
{
	for (QAction* action : menuBar()->actions())
	{
		action->setEnabled(enabled);
	}

	for (DockWidget& dock: m_dockWidgets)
	{
		dock.widget->setEnabled(enabled);
	}
}

void QtMainWindow::refreshStyle()
{
	setStyleSheet(QtResources::loadStyleSheet(QtResources::MAIN_CSS));

	QFont tooltipFont = QToolTip::font();
	tooltipFont.setPixelSize(ApplicationSettings::getInstance()->getFontSize());
	QToolTip::setFont(tooltipFont);
}

void QtMainWindow::setWindowTitleProgress(size_t fileCount, size_t totalFileCount)
{
	m_windowTitleProgress.setProgress(fileCount, totalFileCount);
}

void QtMainWindow::hideWindowTitleProgress()
{
	m_windowTitleProgress.hideProgress();
}

void QtMainWindow::alert()
{
	QApplication::alert(this);
}

void QtMainWindow::showEvent(QShowEvent*  /*e*/)
{
	m_windowTitleProgress.setWindow(this);
}

void QtMainWindow::keyPressEvent(QKeyEvent *event)
{
	switch (QtActions::detectAction(event))
	{
		case Action::UndoHistory:
			MessageHistoryUndo().dispatch();
			break;

		case Action::Cancel:
			emit hideScreenSearch();
			emit hideIndexingDialog();
			break;

		case Action::SearchScreen:
			emit showScreenSearch();
			break;

		case Action::RefreshUI:
			MessageRefreshUI().dispatch();
			break;

		case Action::CloseTab:
			closeTab();
			break;

		case Action::SwitchGraphCodeFocus:
			MessageFocusView(MessageFocusView::ViewType::TOGGLE).dispatch();
			break;

		default:
			if (event->key() == Qt::Key_Space)
				PRINT_TRACES();
			else
				Super::keyPressEvent(event);
			break;
	}
}

void QtMainWindow::contextMenuEvent(QContextMenuEvent* event)
{
	QtContextMenu menu(event, this);
	menu.addUndoActions();
	menu.show();
}

void QtMainWindow::closeEvent(QCloseEvent*  /*event*/)
{
	MessageIndexingInterrupted().dispatchImmediately();
	MessageWindowClosed().dispatchImmediately();
}

void QtMainWindow::resizeEvent(QResizeEvent* event)
{
	m_windowStack.centerSubWindows();
	Super::resizeEvent(event);
}

bool QtMainWindow::focusNextPrevChild(bool  /*next*/)
{
	// makes tab key available in key press event
	return false;
}

void QtMainWindow::about()
{
	QtAbout* aboutWindow = createWindow<QtAbout>();
	aboutWindow->setupAbout();
}

void QtMainWindow::openSettings()
{
	QtPreferencesWindow* window = createWindow<QtPreferencesWindow>();
	window->setup();
}

void QtMainWindow::showDocumentation()
{
	QDesktopServices::openUrl(QUrl(QString::fromStdString(utility::getDocumentationLink())));
}

void QtMainWindow::showKeyboardShortcuts()
{
	QtKeyboardShortcuts* keyboardShortcutWindow = createWindow<QtKeyboardShortcuts>();
	keyboardShortcutWindow->setup();
}

void QtMainWindow::showLegend()
{
	MessageActivateLegend().dispatch();
}

void QtMainWindow::showErrorHelpMessage()
{
	MessageErrorsHelpMessage(true).dispatch();
}

void QtMainWindow::showChangelog()
{
	QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/petermost/Sourcetrail/blob/master/CHANGELOG.md")));
}

void QtMainWindow::showBugtracker()
{
	QDesktopServices::openUrl(
		QUrl(QStringLiteral("https://github.com/petermost/Sourcetrail/issues")));
}

void QtMainWindow::showLicenses()
{
	QtLicenseWindow* licenseWindow = createWindow<QtLicenseWindow>();
	licenseWindow->setup();
}

void QtMainWindow::showDataFolder()
{
	QDesktopServices::openUrl(QUrl(
		QString::fromStdString(
			"file:///" + UserPaths::getUserDataDirectoryPath().makeCanonical().str()),
		QUrl::TolerantMode));
}

void QtMainWindow::showLogFolder()
{
	QDesktopServices::openUrl(QUrl(
		QString::fromStdString(
			"file:///" + ApplicationSettings::getInstance()->getLogDirectoryPath().str()),
		QUrl::TolerantMode));
}

void QtMainWindow::openTab()
{
	MessageTabOpen().dispatch();
}

void QtMainWindow::closeTab()
{
	MessageTabClose().dispatch();
}

void QtMainWindow::nextTab()
{
	MessageTabSelect(true).dispatch();
}

void QtMainWindow::previousTab()
{
	MessageTabSelect(false).dispatch();
}

void QtMainWindow::showStartScreen()
{
	if (dynamic_cast<QtStartScreen*>(m_windowStack.getTopWindow()))
	{
		return;
	}

	QtStartScreen* startScreen = createWindow<QtStartScreen>();
	connect(startScreen, &QtStartScreen::openOpenProjectDialog, this, &QtMainWindow::openProject);
	connect(startScreen, &QtStartScreen::openNewProjectDialog, this, &QtMainWindow::newProject);
}

void QtMainWindow::hideStartScreen()
{
	m_windowStack.clearWindows();
}

void QtMainWindow::newProject()
{
	QtProjectWizard* wizard = createWindow<QtProjectWizard>();
	wizard->newProject();
}

void QtMainWindow::newProjectFromCDB(const FilePath& filePath)
{
	QtProjectWizard* wizard = dynamic_cast<QtProjectWizard*>(m_windowStack.getTopWindow());
	if (!wizard)
	{
		wizard = createWindow<QtProjectWizard>();
	}

	wizard->newProjectFromCDB(filePath);
}

void QtMainWindow::openProject()
{
	QString fileName = QtFileDialog::getOpenFileName(
		this, tr("Open File"), FilePath(), QStringLiteral("Sourcetrail Project Files (*.srctrl.toml)"));

	if (!fileName.isEmpty())
	{
		MessageLoadProject(FilePath(fileName.toStdString())).dispatch();
		m_windowStack.clearWindows();
	}
}

void QtMainWindow::editProject()
{
	std::shared_ptr<const Project> currentProject = Application::getInstance()->getCurrentProject();
	if (currentProject)
	{
		QtProjectWizard* wizard = createWindow<QtProjectWizard>();

		wizard->editProject(currentProject->getProjectSettingsFilePath());
	}
}

void QtMainWindow::closeProject()
{
	if (Application::getInstance()->getCurrentProject())
	{
		MessageCloseProject().dispatch();
		showStartScreen();
	}
}

void QtMainWindow::find()
{
	MessageFind().dispatch();
}

void QtMainWindow::findFulltext()
{
	MessageFind(true).dispatch();
}

void QtMainWindow::findOnScreen()
{
	emit showScreenSearch();
}

void QtMainWindow::codeReferencePrevious()
{
	MessageCodeReference(MessageCodeReference::Type::PREVIOUS, false).dispatch();
}

void QtMainWindow::codeReferenceNext()
{
	MessageCodeReference(MessageCodeReference::Type::NEXT, false).dispatch();
}

void QtMainWindow::codeLocalReferencePrevious()
{
	MessageCodeReference(MessageCodeReference::Type::PREVIOUS, true).dispatch();
}

void QtMainWindow::codeLocalReferenceNext()
{
	MessageCodeReference(MessageCodeReference::Type::NEXT, true).dispatch();
}

void QtMainWindow::customTrail()
{
	MessageCustomTrailShow().dispatch();
}

void QtMainWindow::overview()
{
	MessageActivateOverview().dispatch();
}

void QtMainWindow::closeWindow()
{
	QWidget* activeWindow = QApplication::activeWindow();
	if (activeWindow)
	{
		activeWindow->close();
	}
}

void QtMainWindow::refresh()
{
	MessageIndexingShowDialog().dispatch();
	MessageRefresh().dispatch();
}

void QtMainWindow::forceRefresh()
{
	MessageIndexingShowDialog().dispatch();
	MessageRefresh().refreshAll().dispatch();
}

void QtMainWindow::saveAsImage()
{
	QString filePath = QtFileDialog::showSaveFileDialog(
		this, tr("Save as Image"), FilePath(), "PNG (*.png);;JPEG (*.JPEG);;BMP Files (*.bmp)");
	if (filePath.isNull())
	{
		return;
	}
	MessageSaveAsImage m(filePath);
	m.setSchedulerId(TabIds::currentTab());
	m.dispatch();
}

void QtMainWindow::undo()
{
	MessageHistoryUndo().dispatch();
}

void QtMainWindow::redo()
{
	MessageHistoryRedo().dispatch();
}

void QtMainWindow::zoomIn()
{
	MessageZoom(true).dispatch();
}

void QtMainWindow::zoomOut()
{
	MessageZoom(false).dispatch();
}

void QtMainWindow::resetZoom()
{
	MessageResetZoom().dispatch();
}

void QtMainWindow::resetWindowLayout()
{
	FileSystem::remove(UserPaths::getWindowSettingsFilePath());
	FileSystem::copyFile(
		ResourcePaths::getFallbackDirectoryPath().concatenate("window_settings.ini"),
		UserPaths::getWindowSettingsFilePath());

	applyDefaultDockLayout();
}

void QtMainWindow::applyDefaultDockLayout()
{
	// Classic Sourcetrail arrangement: Search across the top, Graph and Code side by
	// side in the middle, Status across the bottom. Built middle-out — KDDW docks
	// Location_OnTop/OnBottom against the main window (full width) and OnLeft/OnRight
	// split the centre — so ordering Graph, Code, then Search(top), Status(bottom)
	// yields  [ Search ] / [ Graph | Code ] / [ Status ].
	auto dockByName = [this](const char* name) -> KDDockWidgets::QtWidgets::DockWidget* {
		for (const DockWidget& dock: m_dockWidgets)
		{
			if (std::string(dock.view->getName()) == name)
			{
				return dock.widget;
			}
		}
		return nullptr;
	};

	KDDockWidgets::QtWidgets::DockWidget* graph = dockByName("Graph");
	KDDockWidgets::QtWidgets::DockWidget* code = dockByName("Code");
	KDDockWidgets::QtWidgets::DockWidget* search = dockByName("Search");
	KDDockWidgets::QtWidgets::DockWidget* status = dockByName("Status");

	if (graph)
	{
		graph->open();
		addDockWidget(graph, KDDockWidgets::Location_OnLeft);
	}
	if (code)
	{
		code->open();
		addDockWidget(code, KDDockWidgets::Location_OnRight, graph);
	}
	if (search)
	{
		search->open();
		addDockWidget(search, KDDockWidgets::Location_OnTop);
	}
	if (status)
	{
		status->open();
		addDockWidget(status, KDDockWidgets::Location_OnBottom);
	}

	// Any other docks (e.g. plugins) tab into the centre on the right.
	for (const DockWidget& dock: m_dockWidgets)
	{
		const std::string name = dock.view->getName();
		if (name != "Graph" && name != "Code" && name != "Search" && name != "Status")
		{
			dock.widget->open();
			addDockWidget(dock.widget, KDDockWidgets::Location_OnRight);
		}
		dock.action->setChecked(dock.widget->isOpen());
	}
}

void QtMainWindow::openRecentProject()
{
	QAction* action = qobject_cast<QAction*>(sender());
	if (action)
	{
		MessageLoadProject(FilePath(action->data().toString().toStdString())).dispatch();
		m_windowStack.clearWindows();
	}
}

void QtMainWindow::updateRecentProjectsMenu()
{
	m_recentProjectsMenu->clear();

	const std::vector<FilePath> recentProjects =
		ApplicationSettings::getInstance()->getRecentProjects();
	const size_t recentProjectsCount = ApplicationSettings::getInstance()->getMaxRecentProjectsCount();

	for (size_t i = 0; i < recentProjects.size() && i < recentProjectsCount; ++i)
	{
		const FilePath& project = recentProjects[i];
		if (project.exists())
		{
			QAction* recentProject = new QAction(this);
			recentProject->setText(QString::fromStdString(project.fileName()));
			recentProject->setData(QString::fromStdString(project.str()));
			connect(recentProject, &QAction::triggered, this, &QtMainWindow::openRecentProject);
			m_recentProjectsMenu->addAction(recentProject);
		}
	}
}

void QtMainWindow::toggleView(View* view, bool fromMenu)
{
	DockWidget* dock = getDockWidgetForView(view);

	if (fromMenu)
	{
		if (dock->action->isChecked())
		{
			dock->widget->open();
		}
		else
		{
			dock->widget->close();
		}
	}
	else
	{
		dock->action->setChecked(dock->widget->isOpen());
	}
}

void QtMainWindow::toggleShowDockWidgetTitleBars()
{
	setShowDockWidgetTitleBars(!m_showDockWidgetTitleBars);
}

void QtMainWindow::showBookmarkCreator()
{
	MessageBookmarkCreate().dispatch();
}

void QtMainWindow::showBookmarkBrowser()
{
	MessageBookmarkBrowse().dispatch();
}

void QtMainWindow::showBuildJsonBrowser()
{
	const auto app{Application::getInstance()};
	if (!app)
		return;

	const auto project{app->getCurrentProject()};
	if (!project)
	{
		MessageStatus("No project loaded.", true).dispatch();
		return;
	}

	std::shared_ptr<const SourceGroup> sourceGroupWithJson{};
	for (const auto& sourceGroup : project->getSourceGroups())
	{
		const auto snapshot{sourceGroup->getBuildModelSnapshot()};
		if (!snapshot.has_value())
			continue;
		if (snapshot->jsonEntryPoints.empty())
			continue;

		sourceGroupWithJson = sourceGroup;
		break;
	}

	if (!sourceGroupWithJson)
	{
		MessageStatus("No build JSON entry points are available for this project.", true).dispatch();
		return;
	}

	if (!m_buildJsonBrowser)
		m_buildJsonBrowser = new QtBuildJsonBrowser(this);

	m_buildJsonBrowser->setSourceGroup(sourceGroupWithJson);
	m_buildJsonBrowser->show();
	m_buildJsonBrowser->raise();
	m_buildJsonBrowser->activateWindow();
}

void QtMainWindow::openHistoryAction()
{
	QAction* action = qobject_cast<QAction*>(sender());
	if (action)
	{
		std::shared_ptr<MessageBase> m = m_history[action->data().toInt()];
		m->setSchedulerId(TabIds::currentTab());
		m->setIsReplayed(false);
		m->dispatch();
	}
}

void QtMainWindow::activateBookmarkAction()
{
	QAction* action = qobject_cast<QAction*>(sender());
	if (action)
	{
		std::shared_ptr<Bookmark> bookmark = m_bookmarks[action->data().toInt()];
		MessageBookmarkActivate(bookmark).dispatch();
	}
}

void QtMainWindow::setupProjectMenu()
{
	QMenu* menu = new QMenu(tr("&Project"), this);
	menuBar()->addMenu(menu);

	menu->addAction(QtActions::newProject().text(), QtActions::newProject().shortcut(), this, &QtMainWindow::newProject);
	menu->addAction(QtActions::openProject().text(), QtActions::openProject().shortcut(), this, &QtMainWindow::openProject);

	m_recentProjectsMenu = new QMenu(tr("Recent Projects"));
	menu->addMenu(m_recentProjectsMenu);
	updateRecentProjectsMenu();

	menu->addSeparator();

	menu->addAction(tr("&Edit Project..."), this, &QtMainWindow::editProject);

	menu->addSeparator();

	menu->addAction(tr("Close Project"), this, &QtMainWindow::closeProject);
	menu->addAction(QtActions::exit().text(), QtActions::exit().shortcut(), QCoreApplication::instance(), &QCoreApplication::quit);
}

void QtMainWindow::setupEditMenu()
{
	QMenu* menu = new QMenu(tr("&Edit"), this);
	menuBar()->addMenu(menu);

	menu->addAction(QtActions::refresh().text(), QtActions::refresh().shortcut(), this, &QtMainWindow::refresh);
	menu->addAction(QtActions::fullRefresh().text(), QtActions::fullRefresh().shortcut(), this, &QtMainWindow::forceRefresh);

	menu->addSeparator();

	menu->addAction(QtActions::findSymbol().text(), QtActions::findSymbol().shortcut(), this, &QtMainWindow::find);
	menu->addAction(QtActions::findText().text(), QtActions::findText().shortcut(), this, &QtMainWindow::findFulltext);
	menu->addAction(QtActions::findOnScreen().text(), QtActions::findOnScreen().shortcut(), this, &QtMainWindow::findOnScreen);

	menu->addSeparator();

	menu->addAction(QtActions::nextReference().text(), QtActions::nextReference().shortcut(), this, &QtMainWindow::codeReferenceNext);
	menu->addAction(QtActions::previousReference().text(), QtActions::previousReference().shortcut(), this, &QtMainWindow::codeReferencePrevious);
	menu->addAction(QtActions::nextLocalReference().text(), QtActions::nextLocalReference().shortcut(), this, &QtMainWindow::codeLocalReferenceNext);
	menu->addAction(QtActions::previousLocalReference().text(), QtActions::previousLocalReference().shortcut(), this, &QtMainWindow::codeLocalReferencePrevious);

	menu->addSeparator();

	menu->addAction(QtActions::customTrailDialog().text(), QtActions::customTrailDialog().shortcut(), this, &QtMainWindow::customTrail);

	menu->addSeparator();

	menu->addAction(QtActions::toOverview().text(), QtActions::toOverview().shortcut(), this, &QtMainWindow::overview);

	menu->addSeparator();

	menu->addAction(QtActions::saveGraphAsImage().text(), QtActions::saveGraphAsImage().shortcut(), this, &QtMainWindow::saveAsImage);

	menu->addSeparator();

	menu->addAction(QtActions::preferences().text(), QtActions::preferences().shortcut(), this, &QtMainWindow::openSettings);
}

void QtMainWindow::setupViewMenu()
{
	QMenu* menu = new QMenu(tr("&View"), this);
	menuBar()->addMenu(menu);

	menu->addAction(QtActions::newTab().text(), QtActions::newTab().shortcut(), this, &QtMainWindow::openTab);
	menu->addAction(QtActions::closeTab().text(), QtActions::closeTab().shortcut(), this, &QtMainWindow::closeTab);
	menu->addAction(QtActions::selectNextTab().text(), QtActions::selectNextTab().shortcut(), this, &QtMainWindow::nextTab);
	menu->addAction(QtActions::selectPreviousTab().text(), QtActions::selectPreviousTab().shortcut(), this, &QtMainWindow::previousTab);

	menu->addSeparator();

	menu->addAction(tr("Show Start Window"), this, &QtMainWindow::showStartScreen);
	menu->addAction(tr("Show Build JSON Browser"), this, &QtMainWindow::showBuildJsonBrowser);

	m_showTitleBarsAction = new QAction(tr("Show Title Bars"), this);
	m_showTitleBarsAction->setCheckable(true);
	m_showTitleBarsAction->setChecked(m_showDockWidgetTitleBars);
	connect(m_showTitleBarsAction, &QAction::triggered, this, &QtMainWindow::toggleShowDockWidgetTitleBars);
	menu->addAction(m_showTitleBarsAction);

	menu->addAction(tr("Reset Window Layout"), this, &QtMainWindow::resetWindowLayout);

	menu->addSeparator();

	m_viewSeparator = menu->addSeparator();

	menu->addAction(QtActions::largerFont().text(), QtActions::largerFont().shortcut(), this, &QtMainWindow::zoomIn);
	menu->addAction(QtActions::smallerFont().text(), QtActions::smallerFont().shortcut(), this, &QtMainWindow::zoomOut);
	menu->addAction(QtActions::resetFontSize().text(), QtActions::resetFontSize().shortcut(), this, &QtMainWindow::resetZoom);

	m_viewMenu = menu;
}

void QtMainWindow::setupHistoryMenu()
{
	using enum utility::ElideMode;
	if (!m_historyMenu)
	{
		m_historyMenu = new QMenu(tr("&History"), this);
		menuBar()->addMenu(m_historyMenu);
	}
	else
		m_historyMenu->clear();

	m_historyMenu->addAction(QtActions::back().text(), QtActions::back().shortcut(), this, &QtMainWindow::undo);
	m_historyMenu->addAction(QtActions::forward().text(), QtActions::forward().shortcut(), this, &QtMainWindow::redo);

	m_historyMenu->addSection(tr("Recent Symbols"));

	for (size_t i = 0; i < m_history.size(); i++)
	{
		MessageActivateBase* msg = dynamic_cast<MessageActivateBase*>(m_history[i].get());
		if (!msg)
		{
			continue;
		}

		const SearchMatch match = msg->getSearchMatches()[0];
		const std::string name = utility::elide(match.getFullName(), ELIDE_RIGHT, 50);

		QAction* action = new QAction();
		action->setText(QString::fromStdString(name));
		action->setData(QVariant(int(i)));

		connect(action, &QAction::triggered, this, &QtMainWindow::openHistoryAction);
		m_historyMenu->addAction(action);
	}
}

void QtMainWindow::setupBookmarksMenu()
{
	using enum utility::ElideMode;
	if (!m_bookmarksMenu)
	{
		m_bookmarksMenu = new QMenu(tr("&Bookmarks"), this);
		menuBar()->addMenu(m_bookmarksMenu);
	}
	else
		m_bookmarksMenu->clear();

	m_bookmarksMenu->addAction(QtActions::bookmarkActiveSymbol().text(), QtActions::bookmarkActiveSymbol().shortcut(), this, &QtMainWindow::showBookmarkCreator);
	m_bookmarksMenu->addAction(QtActions::bookmarkManager().text(), QtActions::bookmarkManager().shortcut(), this, &QtMainWindow::showBookmarkBrowser);

	m_bookmarksMenu->addSection(tr("Recent Bookmarks"));

	for (size_t i = 0; i < m_bookmarks.size(); i++)
	{
		Bookmark* bookmark = m_bookmarks[i].get();
		std::string name = utility::elide(bookmark->getName(), ELIDE_RIGHT, 50);

		QAction* action = new QAction();
		action->setText(QString::fromStdString(name));
		action->setData(QVariant(int(i)));

		connect(action, &QAction::triggered, this, &QtMainWindow::activateBookmarkAction);
		m_bookmarksMenu->addAction(action);
	}
}

void QtMainWindow::setupHelpMenu()
{
	QMenu* menu = new QMenu(tr("&Help"), this);
	menuBar()->addMenu(menu);

	menu->addAction(QtActions::showKeyboardShortcuts().text(), QtActions::showKeyboardShortcuts().shortcut(), this, &QtMainWindow::showKeyboardShortcuts);
	menu->addAction(QtActions::showLegend().text(), QtActions::showLegend().shortcut(), &QtMainWindow::showLegend);

	menu->addSeparator();

	menu->addAction(tr("Fixing Errors..."), this, &QtMainWindow::showErrorHelpMessage);
	menu->addAction(tr("Documentation..."), this, &QtMainWindow::showDocumentation);
	menu->addAction(tr("Changelog..."), this, &QtMainWindow::showChangelog);
	menu->addAction(tr("Bug Tracker..."), this, &QtMainWindow::showBugtracker);

	menu->addSeparator();
	
	menu->addAction(tr("About Sourcetrail..."), this, &QtMainWindow::about);
	menu->addAction(tr("About Qt..."), this, QApplication::aboutQt);
	menu->addAction(tr("License..."), this, &QtMainWindow::showLicenses);

	menu->addSeparator();

	menu->addAction(tr("Show Data Folder..."), this, &QtMainWindow::showDataFolder);
	menu->addAction(tr("Show Log Folder..."), this, &QtMainWindow::showLogFolder);
}

QtMainWindow::DockWidget* QtMainWindow::getDockWidgetForView(View* view)
{
	for (DockWidget& dock: m_dockWidgets)
	{
		if (dock.view == view)
		{
			return &dock;
		}

		const CompositeView* compositeView = dynamic_cast<const CompositeView*>(dock.view);
		if (compositeView)
		{
			for (const View* v: compositeView->getViews())
			{
				if (v == view)
				{
					return &dock;
				}
			}
		}

		const TabbedView* tabbedView = dynamic_cast<const TabbedView*>(dock.view);
		if (tabbedView)
		{
			for (const View* v: tabbedView->getViews())
			{
				if (v == view)
				{
					return &dock;
				}
			}
		}
	}

	LOG_ERROR("DockWidget was not found for view.");
	return nullptr;
}

void QtMainWindow::setShowDockWidgetTitleBars(bool showTitleBars)
{
	m_showDockWidgetTitleBars = showTitleBars;

	if (m_showTitleBarsAction)
	{
		m_showTitleBarsAction->setChecked(showTitleBars);
	}

	// KDDW manages title bars globally through Config flags (not per-dock features as
	// QDockWidget did). The default (title bars OFF) is the thin "tab-strip handle"
	// look: no title bar row — each dock shows a slim tab (its name + close/float
	// buttons) that doubles as the drag handle. AlwaysShowTabs keeps that handle on
	// single, untabbed docks too. Toggling the View-menu action ON restores classic
	// full title bars (and tabs only appear when docks are actually tabbed together).
	// Flags apply cleanly when set before any dock exists — the case on the
	// constructor's first loadLayout() call; a later toggle takes full effect next launch.
	const KDDockWidgets::Config::Flags handleFlags =
		KDDockWidgets::Config::Flag_HideTitleBarWhenTabsVisible |
		KDDockWidgets::Config::Flag_AlwaysShowTabs |
		KDDockWidgets::Config::Flag_ShowButtonsOnTabBarIfTitleBarHidden;

	auto& config = KDDockWidgets::Config::self();
	auto flags = config.flags();
	if (showTitleBars)
	{
		flags &= ~handleFlags;
	}
	else
	{
		flags |= handleFlags;
	}
	config.setFlags(flags);
}

template <typename T>
T* QtMainWindow::createWindow()
{
	T* window = new T(this);

	connect(window, &QtWindow::canceled, &m_windowStack, &QtWindowStack::popWindow);
	connect(window, &QtWindow::finished, &m_windowStack, &QtWindowStack::clearWindows);

	m_windowStack.pushWindow(window);

	return window;
}
