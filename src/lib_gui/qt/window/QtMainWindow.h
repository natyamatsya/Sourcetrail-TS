#ifndef QT_MAIN_WINDOW_H
#define QT_MAIN_WINDOW_H

#include <QPointer>

#include "FilePath.h"
#include "PreferencesStateMachine.h"
#include "ProjectWizardStateMachine.h"
#include "QtWindowStack.h"
#include "QtWindowTitleProgress.h"

#include <kddockwidgets/MainWindow.h>

#include <memory>
#include <optional>
#include <vector>

class Bookmark;
class MessageBase;
class QtBuildJsonBrowser;
class QtDockControl;
class QtPreferencesWindow;
class QtProjectWizard;
class View;

namespace KDDockWidgets::QtWidgets
{
class DockWidget;
}

class QtViewToggle: public QWidget
{
	Q_OBJECT

public:
	QtViewToggle(View* view, QWidget* parent = nullptr);
	void clear();

public slots:
	void toggledByAction();
	void toggledByUI();

private:
	View* m_view;
};


class MouseReleaseFilter: public QObject
{
	Q_OBJECT

public:
	MouseReleaseFilter(QObject* parent);

protected:
	bool eventFilter(QObject* obj, QEvent* event) override;

private:
	size_t m_backButton;
	size_t m_forwardButton;
};


class QtMainWindow: public KDDockWidgets::QtWidgets::MainWindow
{
	Q_OBJECT

	// KDDockWidgets::QtWidgets::MainWindow is-a QMainWindow (via View<QMainWindow>),
	// so menus/toolbars/status bar keep working; call Super:: in event overrides so
	// KDDW's own layouting still runs.
	using Super = KDDockWidgets::QtWidgets::MainWindow;

	// The KDDW base inherits View<QMainWindow>, whose injected-class-name 'View' would
	// otherwise shadow Sourcetrail's ::View for every unqualified use inside this class
	// (addView(View*), the DockWidget struct's View* member, etc.). Re-bind it.
	using View = ::View;

public:
	QtMainWindow();
	~QtMainWindow() override;

	void addView(View* view);
	void overrideView(View* view);
	void removeView(View* view);

	void showView(View* view);
	void hideView(View* view);

	View* findFloatingView(const std::string& name) const;

	void loadLayout();
	void saveLayout();

	// Sets KDDockWidgets' flags for the views' title bars as the window settings say
	// (View > Show Title Bars). KDDW takes them only before a dock or main window
	// exists: call it before the main window is created.
	static void applyDockWidgetTitleBarSetting();

	// Repair a degenerate restored main-window geometry (minimized / off-screen /
	// too small) so the window is always visible and usable after loadLayout().
	void sanitizeWindowGeometry();

	// Arrange the docks in the classic Sourcetrail default layout (Search top,
	// Graph | Code middle, Status bottom). Used on first run and on reset.
	void applyDefaultDockLayout();

	// The Search card holds one row of controls. The layout gives every card its share of the
	// window, so a growing window would grow empty space under that row: trim the card back
	// to its content, after the window has its size. (Queued: the docks get their new sizes
	// after the window's resize event.)
	void fitSearchDockToContent();
	void fitSearchDockToContentLater();

	void loadDockWidgetLayout();
	void loadWindow(bool showStartWindow);

	void updateHistoryMenu(std::shared_ptr<MessageBase> message);
	void clearHistoryMenu();

	void updateBookmarksMenu(const std::vector<std::shared_ptr<Bookmark>>& bookmarks);

	void setContentEnabled(bool enabled);
	void refreshStyle();

	void setWindowTitleProgress(size_t fileCount, size_t totalFileCount);
	void hideWindowTitleProgress();

	void alert();
	
signals:
	void showScreenSearch();
	void hideScreenSearch();
	void hideIndexingDialog();

protected:
	void showEvent(QShowEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void contextMenuEvent(QContextMenuEvent* event) override;
	void closeEvent(QCloseEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

	bool focusNextPrevChild(bool next) override;

public slots:
	void about();
	void openSettings();
	static void showChangelog();
	static void showBugtracker();
	static void showDocumentation();
	void showKeyboardShortcuts();
	static void showLegend();

	static void showErrorHelpMessage();
	void showLicenses();

	static void showDataFolder();
	static void showLogFolder();

	static void openTab();
	static void closeTab();
	static void nextTab();
	static void previousTab();

	void showStartScreen();
	void hideStartScreen();

	void newProject();
	void newProjectFromCDB(const FilePath& filePath);
	void openProject();
	void editProject();
	void closeProject();

	// What the project wizard machine has the wizard do for an agent
	// (PresentsProjectWizard): open it, or pass the request on to it.
	void applyToProjectWizard(const ProjectWizardRequest& request);

	// What the preferences machine has the Preferences dialog do for an agent
	// (PresentsPreferences): open it, or pass the request on to it.
	void applyToPreferences(const PreferencesRequest& request);

	static void find();
	static void findFulltext();
	void findOnScreen();
	static void codeReferencePrevious();
	static void codeReferenceNext();
	static void codeLocalReferencePrevious();
	static void codeLocalReferenceNext();
	static void customTrail();
	static void overview();

	static void closeWindow();
	static void refresh();
	static void forceRefresh();

	static void undo();
	static void redo();
	static void zoomIn();
	static void zoomOut();
	static void resetZoom();

	void resetWindowLayout();

	void openRecentProject();
	void updateRecentProjectsMenu();

	void toggleView(View* view, bool fromMenu);
	void saveAsImage();

private slots:
	void toggleShowDockWidgetTitleBars();
	void showBuildJsonBrowser();

	static void showBookmarkCreator();
	static void showBookmarkBrowser();

	void openHistoryAction();
	void activateBookmarkAction();

private:
	struct DockWidget
	{
		KDDockWidgets::QtWidgets::DockWidget* widget;
		View* view;
		QAction* action;
		QtViewToggle* toggle;
	};

	void setupEditMenu();
	void setupProjectMenu();
	void setupViewMenu();
	void setupHistoryMenu();
	void setupBookmarksMenu();
	void setupHelpMenu();

	DockWidget* getDockWidgetForView(View* view);

	void setShowDockWidgetTitleBars(bool showTitleBars);

	template <typename T>
	T* createWindow();

	std::vector<DockWidget> m_dockWidgets;

	QMenu* m_viewMenu;
	QAction* m_viewSeparator;

	QMenu* m_historyMenu = nullptr;
	std::vector<std::shared_ptr<MessageBase>> m_history;

	QMenu* m_bookmarksMenu = nullptr;
	std::vector<std::shared_ptr<Bookmark>> m_bookmarks;

	QMenu* m_recentProjectsMenu;

	QAction* m_showTitleBarsAction;

	// Default ON; OFF: thin tab-strip drag handles instead of full title bars.
	bool m_showDockWidgetTitleBars = true;
	// What the docks show: the setting as it was when they were created (KDDW's flags
	// take effect on docks created afterwards, so a toggle waits for the next start).
	std::optional<bool> m_dockWidgetTitleBarsShown;

	QtWindowStack m_windowStack;
	QtBuildJsonBrowser* m_buildJsonBrowser = nullptr;
	QPointer<QtProjectWizard> m_projectWizard;	// the wizard open, if one is
	QPointer<QtPreferencesWindow> m_preferencesWindow;	// the Preferences open, if they are
	std::unique_ptr<QtDockControl> m_dockControl;	 // the docks for the docks machine

	QtWindowTitleProgress m_windowTitleProgress;
};

#endif	  // QT_MAIN_WINDOW_H
