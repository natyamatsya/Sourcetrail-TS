#include "QtDialogView.h"
#include "QtMessageBox.h"

#include <chrono>
#include <sstream>
#include <thread>

#include <QGuiApplication>
#include <QTimer>

#ifndef SRCTRL_MODULE_BUILD
#include "MessageIndexingStatus.h"
#include "MessageStatus.h"
#endif
#include "Project.h"
#include "QtIndexingDialog.h"
#include "QtIndexingProgressDialog.h"
#include "QtIndexingReportDialog.h"
#include "QtIndexingStartDialog.h"
#include "QtKnownProgressDialog.h"
#include "QtMainWindow.h"
#include "QtUnknownProgressDialog.h"
#include "QtWindow.h"
#ifndef SRCTRL_MODULE_BUILD
#include "StorageAccess.h"
#endif
#include "UiPost.h"
#include "TabIds.h"
#include "TaskLambda.h"
#ifndef SRCTRL_MODULE_BUILD
#include "utility.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.messaging;
import srctrl.storage;
import srctrl.utility;
#endif

QtDialogView::QtDialogView(QtMainWindow* mainWindow, UseCase useCase, StorageAccess* storageAccess)
	: DialogView(useCase, storageAccess)
	, m_mainWindow(mainWindow)
	, m_windowStack(this)
{
}

QtDialogView::~QtDialogView()
{
	m_resultReady = true;
}

bool QtDialogView::dialogsHidden() const
{
	return !m_dialogsVisible;
}

void QtDialogView::clearDialogs()
{
	execution::qt::onUi(m_mainWindow, [=, this]() {
		m_windowStack.clearWindows();

		setUIBlocked(false);
	});

	setParentWindow(nullptr);
}

void QtDialogView::showUnknownProgressDialog(const std::string& title, const std::string& message)
{
	MessageStatus(title + ": " + message, false, true).dispatch();

	execution::qt::onUi(m_mainWindow, [=, this]() { showUnknownProgress(title, message, false); });
}

void QtDialogView::hideUnknownProgressDialog()
{
	MessageStatus("", false, false).dispatch();

	execution::qt::onUi(m_mainWindow, [=, this]() { hideUnknownProgress(); });

	setParentWindow(nullptr);
}

void QtDialogView::showProgressDialog(
	const std::string& title, const std::string& message, size_t progress)
{
	execution::qt::onUi(m_mainWindow, [=, this]() {
		bool sendStatusMessage = true;
		QtKnownProgressDialog* window = dynamic_cast<QtKnownProgressDialog*>(
			m_windowStack.getTopWindow());
		if (!window)
		{
			m_windowStack.clearWindows();
			window = createWindow<QtKnownProgressDialog>(m_dialogsHideable);
		}
		else
		{
			sendStatusMessage =
				(window->getTitle() != title || window->getMessage() != message ||
				 window->getProgress() != progress);
		}

		if (sendStatusMessage)
		{
			MessageStatus(
				title + ": " + message + " [" + std::to_string(progress) + "%]", false, true)
				.dispatch();
		}

		window->updateTitle(QString::fromStdString(title));
		window->updateMessage(QString::fromStdString(message));
		window->updateProgress(progress);

		setUIBlocked(m_dialogsVisible);
	});
}

void QtDialogView::hideProgressDialog()
{
	execution::qt::onUi(m_mainWindow, [=, this]() {
		QtKnownProgressDialog* window = dynamic_cast<QtKnownProgressDialog*>(
			m_windowStack.getTopWindow());
		if (window)
		{
			m_windowStack.popWindow();
		}

		MessageStatus("", false, false).dispatch();

		setUIBlocked(false);
	});

	setParentWindow(nullptr);
}

void QtDialogView::startIndexingDialog(
	Project* project,
	const std::vector<RefreshMode>& enabledModes,
	const RefreshMode initialMode,
	std::function<void(const RefreshInfo& info)> onStartIndexing,
	std::function<void()> onCancelIndexing)
{
	m_refreshInfos.clear();

	execution::qt::onUi(m_mainWindow, [=, this]() {
		m_dialogsVisible = true;
		m_windowStack.clearWindows();

		QtIndexingStartDialog* window = createWindow<QtIndexingStartDialog>(
			enabledModes, initialMode);

		std::function<void(RefreshMode)> onRefreshModeChanged = ([=, this](RefreshMode refreshMode) {
			auto it = m_refreshInfos.find(refreshMode);
			if (it != m_refreshInfos.end())
			{
				window->updateRefreshInfo(it->second);
				window->show();
				return;
			}

			std::shared_ptr<QTimer> timer = std::make_shared<QTimer>();
			timer->setSingleShot(true);
			connect(timer.get(), &QTimer::timeout, [=, this]() {
				showUnknownProgress("Preparing Indexing", "Processing Files", true);
			});
			timer->start(200);

			Task::dispatch(TabIds::app(), std::make_shared<TaskLambda>([=, this]() {
							   RefreshInfo info = project->getRefreshInfo(refreshMode);

							   execution::qt::onUi(m_mainWindow, [=, this]() {
								   m_dialogsVisible = true;
								   m_refreshInfos.emplace(info.mode, info);
								   window->updateRefreshInfo(info);

								   timer->stop();
								   hideUnknownProgress();
								   window->show();
							   });
						   }));
		});

		connect(window, &QtIndexingStartDialog::setMode, onRefreshModeChanged);

		connect(window, &QtIndexingStartDialog::startIndexing, [=, this](RefreshMode refreshMode) {
			RefreshInfo info = m_refreshInfos.find(refreshMode)->second;
			Task::dispatch(
				TabIds::app(), std::make_shared<TaskLambda>([=]() { onStartIndexing(info); }));

			m_windowStack.clearWindows();
		});

		connect(window, &QtIndexingDialog::canceled, [=, this]() {
			Task::dispatch(TabIds::app(), std::make_shared<TaskLambda>([=]() { onCancelIndexing(); }));

			setUIBlocked(false);
		});

		onRefreshModeChanged(initialMode);

		window->hide();
		setUIBlocked(true);
	});
}

void QtDialogView::updateIndexingDialog(
	size_t startedFileCount,
	size_t finishedFileCount,
	size_t totalFileCount,
	const std::vector<FilePath>& sourcePaths)
{
	execution::qt::onUi(m_mainWindow, [=, this]() {
		if (!sourcePaths.empty())
		{
			std::vector<std::string> stati;
			for (const FilePath& path: sourcePaths)
			{
				stati.push_back(
					"[" + std::to_string(startedFileCount) + "/" +
					std::to_string(totalFileCount) + "] Indexing file: " + path.str());
			}
			MessageStatus(stati, false, false, m_dialogsVisible).dispatch();
		}

		QtIndexingProgressDialog* window = dynamic_cast<QtIndexingProgressDialog*>(
			m_windowStack.getTopWindow());
		if (!window)
		{
			m_windowStack.clearWindows();

			window = createWindow<QtIndexingProgressDialog>(m_dialogsHideable);
		}

		window->updateIndexingProgress(finishedFileCount, totalFileCount, sourcePaths.empty() ? FilePath() : sourcePaths.back());

		m_mainWindow->setWindowTitleProgress(finishedFileCount, totalFileCount);

		setUIBlocked(m_dialogsVisible);
	});
}

void QtDialogView::updateCustomIndexingDialog(
	size_t startedFileCount,
	size_t finishedFileCount,
	size_t totalFileCount,
	const std::vector<FilePath>& sourcePaths)
{
	updateIndexingDialog(startedFileCount, finishedFileCount, totalFileCount, sourcePaths);

	execution::qt::onUi(m_mainWindow, [=, this]() {
		QtIndexingProgressDialog* window = dynamic_cast<QtIndexingProgressDialog*>(
			m_windowStack.getTopWindow());
		if (window)
		{
			window->updateTitle(QStringLiteral("Executing Commands"));
		}
	});
}

DatabasePolicy QtDialogView::finishedIndexingDialog(
	size_t indexedFileCount,
	size_t totalIndexedFileCount,
	size_t completedFileCount,
	size_t totalFileCount,
	float time,
	ErrorCountInfo errorInfo,
	bool interrupted)
{
	using enum DatabasePolicy;

	// Offscreen (agent-driven / headless GUI) runs have no user who could press
	// the report dialog's buttons. Blocking here parks the task scheduler in a
	// wait loop, which also starves agent-control commands — so keep the indexed
	// data and continue immediately.
	if (QGuiApplication::platformName() == QLatin1String("offscreen"))
	{
		return DATABASE_POLICY_KEEP;
	}

	DatabasePolicy policy = DATABASE_POLICY_UNKNOWN;
	m_resultReady = false;

	execution::qt::onUi(m_mainWindow, [=, this, &policy]() {
		m_dialogsVisible = true;
		m_windowStack.clearWindows();

		QtIndexingReportDialog* window = createWindow<QtIndexingReportDialog>(
			indexedFileCount,
			totalIndexedFileCount,
			completedFileCount,
			totalFileCount,
			time,
			interrupted);
		window->updateErrorCount(errorInfo.total, errorInfo.fatal);
		connect(window, &QtIndexingDialog::finished, [this, &policy]() {
			setUIBlocked(false);
			policy = DATABASE_POLICY_KEEP;
			m_resultReady = true;
		});
		connect(window, &QtIndexingDialog::canceled, [this, &policy]() {
			setUIBlocked(false);
			policy = DATABASE_POLICY_DISCARD;
			m_resultReady = true;
		});
		connect(window, &QtIndexingReportDialog::requestReindexing, [this, &policy]() {
			setUIBlocked(false);
			policy = DATABASE_POLICY_REFRESH;
			m_resultReady = true;
		});

		m_mainWindow->hideWindowTitleProgress();
		m_mainWindow->alert();
		
		setUIBlocked(true);
	});

	while (!m_resultReady)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(25));
	}

	return policy;
}

int QtDialogView::confirm(const std::string& message, const std::vector<std::string>& options)
{
	int result = -1;
	m_resultReady = false;

	execution::qt::onUi(m_mainWindow, [=, this, &result]() {
		QtMessageBox msgBox;
		msgBox.setText(QString::fromStdString(message));

		for (const std::string& option: options)
		{
			msgBox.addButton(QString::fromStdString(option), QtMessageBox::AcceptRole);
		}

		QAbstractButton *clickedButton = msgBox.execModal();

		for (int i = 0; i < msgBox.buttons().size(); i++)
		{
			if (clickedButton == msgBox.buttons().at(i))
			{
				result = i;
				break;
			}
		}

		m_resultReady = true;
	});

	while (!m_resultReady)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(25));
	}

	return result;
}

void QtDialogView::setParentWindow(QtWindow* window)
{
	execution::qt::onUi(m_mainWindow, [=, this]() { m_parentWindow = window; });
}

void QtDialogView::showUnknownProgress(const std::string& title, const std::string& message, bool stacked)
{
	QtUnknownProgressDialog* window = nullptr;

	if (!stacked)
	{
		window = dynamic_cast<QtUnknownProgressDialog*>(m_windowStack.getTopWindow());

		if (!window)
		{
			m_windowStack.clearWindows();
			window = nullptr;
		}
	}

	if (!window)
	{
		window = createWindow<QtUnknownProgressDialog>(m_dialogsHideable);
	}

	window->updateTitle(QString::fromStdString(title));
	window->updateMessage(QString::fromStdString(message));

	setUIBlocked(m_dialogsVisible);
}

void QtDialogView::hideUnknownProgress()
{
	QtUnknownProgressDialog* window = dynamic_cast<QtUnknownProgressDialog*>(
		m_windowStack.getTopWindow());
	if (window)
	{
		m_windowStack.popWindow();
	}

	if (m_windowStack.getWindowCount() == 0)
	{
		setUIBlocked(false);
	}
}

void QtDialogView::setUIBlocked(bool blocked)
{
	if (m_uiBlocked == blocked)
	{
		return;
	}

	m_uiBlocked = blocked;

	if (m_parentWindow)
	{
		m_parentWindow->setEnabled(!blocked);
	}
	else
	{
		m_mainWindow->setContentEnabled(!blocked);
	}

	if (blocked)
	{
		QWidget* window = m_windowStack.getTopWindow();
		if (window)
		{
			window->setEnabled(true);
		}
	}
}

void QtDialogView::dialogVisibilityChanged(bool visible)
{
	QtWindowStackElement* window = m_windowStack.getTopWindow();
	if (!window)
	{
		return;
	}

	window->setVisible(visible);
	m_dialogsVisible = visible;
	setUIBlocked(visible);

	if (!visible && dynamic_cast<QtIndexingDialog*>(window))
	{
		MessageStatus("", false, false).dispatch();
	}
}

void QtDialogView::handleMessage(MessageIndexingShowDialog*  /*message*/)
{
	execution::qt::onUi(m_mainWindow, [=, this]() { dialogVisibilityChanged(true); });
}

void QtDialogView::handleMessage(MessageErrorCountUpdate* message)
{
	ErrorCountInfo errorInfo = message->errorCount;

	execution::qt::onUi(m_mainWindow, [=, this]() { updateErrorCount(errorInfo.total, errorInfo.fatal); });
}

void QtDialogView::handleMessage(MessageWindowClosed*  /*message*/)
{
	m_resultReady = true;
}

void QtDialogView::updateErrorCount(size_t errorCount, size_t fatalCount)
{
	if (QtIndexingProgressDialog* progressWindow = dynamic_cast<QtIndexingProgressDialog*>(
			m_windowStack.getTopWindow()))
	{
		progressWindow->updateErrorCount(errorCount, fatalCount);
	}
	else if (
		QtIndexingReportDialog* reportWindow = dynamic_cast<QtIndexingReportDialog*>(
			m_windowStack.getTopWindow()))
	{
		reportWindow->updateErrorCount(errorCount, fatalCount);
	}
}

template <typename DialogType, typename... ParamTypes>
DialogType* QtDialogView::createWindow(ParamTypes... params)
{
	DialogType* window = nullptr;
	if (m_parentWindow)
	{
		window = new DialogType(params..., m_parentWindow);
	}
	else
	{
		window = new DialogType(params..., m_mainWindow);
	}

	connect(window, &QtIndexingDialog::canceled, &m_windowStack, &QtWindowStack::popWindow);
	connect(window, &QtIndexingDialog::finished, &m_windowStack, &QtWindowStack::clearWindows);
	connect(window, &QtIndexingDialog::visibleChanged, this, &QtDialogView::dialogVisibilityChanged);

	if (m_mainWindow)
	{
		connect(m_mainWindow, &QtMainWindow::hideIndexingDialog, [this] { dialogVisibilityChanged(true); });
	}

	m_windowStack.pushWindow(window);

	if (!m_dialogsVisible)
	{
		window->hide();
	}

	return window;
}
