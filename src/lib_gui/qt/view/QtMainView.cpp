#include "QtMainView.h"
#include "UiPost.h"

#ifndef SRCTRL_MODULE_BUILD
#include "MessageRefreshUIState.h"
#endif
#include "QtMainWindow.h"
#include "QtViewWidgetWrapper.h"
#ifndef SRCTRL_MODULE_BUILD
#include "utilityApp.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.messaging;
import srctrl.process;
#endif

QtMainView::QtMainView(const ViewFactory* viewFactory, StorageAccess* storageAccess)
	: MainView(viewFactory, storageAccess)
{
	m_window = new QtMainWindow();
	m_window->show();
}

QtMainView::~QtMainView()
{
	// clear components to avoid double deletion of views when destroying m_window
	m_componentManager.clear();
	m_window->deleteLater();
}

QtMainWindow* QtMainView::getMainWindow() const
{
	return m_window;
}

void QtMainView::addView(View* view)
{
	m_views.push_back(view);
	m_window->addView(view);
}

void QtMainView::overrideView(View* view)
{
	m_window->overrideView(view);
}

void QtMainView::removeView(View* view)
{
	std::vector<View*>::iterator it = std::find(m_views.begin(), m_views.end(), view);
	if (it == m_views.end())
	{
		return;
	}

	m_window->removeView(view);
	m_views.erase(it);
}

void QtMainView::showView(View* view)
{
	execution::qt::onUi(m_window, [=, this]() { m_window->showView(view); });
}

void QtMainView::hideView(View* view)
{
	execution::qt::onUi(m_window, [=, this]() { m_window->hideView(view); });
}

void QtMainView::setViewEnabled(View* view, bool enabled)
{
	execution::qt::onUi(m_window, [=]() {
		QWidget* widget = QtViewWidgetWrapper::getWidgetOfView(view);
		widget->setEnabled(enabled);
	});
}

View* QtMainView::findFloatingView(const std::string& name) const
{
	return m_window->findFloatingView(name);
}

void QtMainView::showOriginalViews()
{
	for (View* view: m_views)
	{
		m_window->overrideView(view);
	}
}

void QtMainView::loadLayout()
{
	m_window->loadLayout();
}

void QtMainView::saveLayout()
{
	m_window->saveLayout();
}

void QtMainView::loadWindow(bool showStartWindow)
{
	execution::qt::onUi(m_window, [=, this]() { m_window->loadWindow(showStartWindow); });
}

void QtMainView::refreshView()
{
	execution::qt::onUi(m_window, [=, this]() { m_window->refreshStyle(); });
}

void QtMainView::refreshUIState(bool isAfterIndexing)
{
	execution::qt::onUi(m_window, [=]() { MessageRefreshUIState(isAfterIndexing).dispatch(); });
}

QStatusBar* QtMainView::getStatusBar()
{
	return m_window->statusBar();
}

void QtMainView::setStatusBar(QStatusBar* statusbar)
{
	m_window->setStatusBar(statusbar);
}

void QtMainView::hideStartScreen()
{
	execution::qt::onUi(m_window, [=, this]() { m_window->hideStartScreen(); });
}

void QtMainView::setTitle(const std::string& title)
{
	execution::qt::onUi(m_window, [=, this]() { m_window->setWindowTitle(QString::fromStdString(title)); });
}

void QtMainView::activateWindow()
{
	execution::qt::onUi(m_window, [=, this]() {
		// It's platform dependent which of these commands does the right thing, for now we just use
		// them all at once.
		m_window->activateWindow();
		m_window->setEnabled(true);
		m_window->raise();
		m_window->setFocus(Qt::ActiveWindowFocusReason);
		m_window->setWindowState(m_window->windowState() & ~Qt::WindowMinimized);
	});
}

void QtMainView::updateRecentProjectMenu()
{
	execution::qt::onUi(m_window, [=, this]() { m_window->updateRecentProjectsMenu(); });
}

void QtMainView::updateHistoryMenu(std::shared_ptr<MessageBase> message)
{
	execution::qt::onUi(m_window, [=, this]() { m_window->updateHistoryMenu(message); });
}

void QtMainView::clearHistoryMenu()
{
	execution::qt::onUi(m_window, [=, this]() { m_window->clearHistoryMenu(); });
}

void QtMainView::updateBookmarksMenu(const std::vector<std::shared_ptr<Bookmark>>& bookmarks)
{
	execution::qt::onUi(m_window, [=, this]() { m_window->updateBookmarksMenu(bookmarks); });
}

void QtMainView::clearBookmarksMenu()
{
	updateBookmarksMenu({});
}

void QtMainView::handleMessage(MessageProjectEdit*  /*message*/)
{
	execution::qt::onUi(m_window, [=, this]() { m_window->editProject(); });
}

void QtMainView::handleMessage(MessageProjectNew* message)
{
	FilePath cdbPath = message->cdbPath;

	execution::qt::onUi(m_window, [=, this]() { m_window->newProjectFromCDB(cdbPath); });
}

void QtMainView::handleMessage(MessageWindowChanged*  /*message*/)
{
	// Fixes an issue where newly added QtWidgets don't fully respond to focus events on macOS
	if constexpr (utility::Platform::isMac())
	{
		execution::qt::onUi(m_window, [=, this]() {
			m_window->hide();
			m_window->show();
		});
	}
}
