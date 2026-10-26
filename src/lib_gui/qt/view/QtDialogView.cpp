#include "QtDialogView.h"
#include "MessageCause.h"
#include "QtMessageBox.h"

#include <chrono>
#include <sstream>
#include <thread>

#include <QApplication>
#include <QGuiApplication>
#include <QSignalBlocker>
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

QtDialogView::~QtDialogView() = default;

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

bool QtDialogView::presentsQuestions() const
{
	return true;
}

void QtDialogView::presentModeChoice(const std::vector<RefreshMode>& modes, RefreshMode mode)
{
	execution::qt::onUi(m_mainWindow, [=, this]() {
		m_dialogsVisible = true;
		m_windowStack.clearWindows();

		QtIndexingStartDialog* window = createWindow<QtIndexingStartDialog>(modes, mode);
		m_startDialog = window;

		// The buttons send the indexing machine's events, as an agent does; the
		// machine answers through presentRefreshInfo and dismissModeChoice.
		connect(window, &QtIndexingStartDialog::setMode, [](RefreshMode refreshMode) {
			IndexingStateMachine::instance()->selectMode(refreshMode, EventSource::Ui);
		});

		connect(window, &QtIndexingStartDialog::startIndexing, [this](RefreshMode refreshMode) {
			IndexingStateMachine::instance()->start(refreshMode, EventSource::Ui);
			stopCounting();
			m_windowStack.clearWindows();
		});

		connect(window, &QtIndexingDialog::canceled, [this]() {
			IndexingStateMachine::instance()->cancel(EventSource::Ui);
			stopCounting();
			setUIBlocked(false);
		});

		window->hide();	 // until the files of the initial mode are counted
		setUIBlocked(true);
	});
}

void QtDialogView::presentCounting()
{
	execution::qt::onUi(m_mainWindow, [this]() {
		if (m_countingTimer == nullptr)
		{
			m_countingTimer = new QTimer(this);
			m_countingTimer->setSingleShot(true);
			connect(m_countingTimer, &QTimer::timeout, [this]() {
				showUnknownProgress("Preparing Indexing", "Processing Files", true);
			});
		}
		m_countingTimer->start(200);
	});
}

void QtDialogView::presentRefreshInfo(const RefreshInfo& info)
{
	execution::qt::onUi(m_mainWindow, [=, this]() {
		stopCounting();
		if (m_startDialog)
		{
			m_dialogsVisible = true;
			// Showing the mode checks its button, which would report a selection.
			const QSignalBlocker blocker(m_startDialog.data());
			m_startDialog->updateRefreshInfo(info);
			m_startDialog->show();
		}
	});
}

void QtDialogView::dismissModeChoice(bool started)
{
	execution::qt::onUi(m_mainWindow, [=, this]() {
		stopCounting();
		if (!m_startDialog)
		{
			return;
		}
		m_startDialog = nullptr;
		m_windowStack.clearWindows();
		if (!started)
		{
			setUIBlocked(false);
		}
	});
}

void QtDialogView::stopCounting()
{
	if (m_countingTimer != nullptr)
	{
		m_countingTimer->stop();
	}
	hideUnknownProgress();
}

void QtDialogView::showIndexingProgress(
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

void QtDialogView::showCustomIndexingProgress(
	size_t startedFileCount,
	size_t finishedFileCount,
	size_t totalFileCount,
	const std::vector<FilePath>& sourcePaths)
{
	showIndexingProgress(startedFileCount, finishedFileCount, totalFileCount, sourcePaths);

	execution::qt::onUi(m_mainWindow, [=, this]() {
		QtIndexingProgressDialog* window = dynamic_cast<QtIndexingProgressDialog*>(
			m_windowStack.getTopWindow());
		if (window)
		{
			window->updateTitle(QStringLiteral("Executing Commands"));
		}
	});
}

bool QtDialogView::presentReport(const IndexingReport& report)
{
	using enum DatabasePolicy;

	// Offscreen (agent-driven / headless GUI) runs have no user who could press the
	// report's buttons, and a report nobody answers holds the pipeline: the run
	// keeps its data instead.
	if (QGuiApplication::platformName() == QLatin1String("offscreen"))
	{
		return false;
	}

	execution::qt::onUi(m_mainWindow, [=, this]() {
		m_dialogsVisible = true;
		m_windowStack.clearWindows();

		QtIndexingReportDialog* window = createWindow<QtIndexingReportDialog>(
			report.indexedFileCount,
			report.totalIndexedFileCount,
			report.completedFileCount,
			report.totalFileCount,
			report.time,
			report.interrupted);
		window->updateErrorCount(report.errorCount, report.fatalCount);
		connect(window, &QtIndexingDialog::finished, [this]() {
			setUIBlocked(false);
			IndexingStateMachine::instance()->decide(DATABASE_POLICY_KEEP, EventSource::Ui);
		});
		connect(window, &QtIndexingDialog::canceled, [this]() {
			setUIBlocked(false);
			IndexingStateMachine::instance()->decide(DATABASE_POLICY_DISCARD, EventSource::Ui);
		});
		connect(window, &QtIndexingReportDialog::requestReindexing, [this]() {
			setUIBlocked(false);
			IndexingStateMachine::instance()->decide(DATABASE_POLICY_REFRESH, EventSource::Ui);
		});

		m_mainWindow->hideWindowTitleProgress();
		m_mainWindow->alert();

		setUIBlocked(true);
	});
	return true;
}

void QtDialogView::dismissReport()
{
	execution::qt::onUi(m_mainWindow, [this]() {
		if (dynamic_cast<QtIndexingReportDialog*>(m_windowStack.getTopWindow()))
		{
			m_windowStack.clearWindows();
			setUIBlocked(false);
		}
	});
}

void QtDialogView::presentQuestion(
	std::uint64_t id, const std::string& text, const std::vector<std::string>& options)
{
	// On the GUI thread this runs inline and returns once the question is answered;
	// from another thread the asker waits in the question machine meanwhile.
	execution::qt::onUi(m_mainWindow, [=, this]() {
		// An agent answered before the box came up: nothing to show.
		if (!QuestionStateMachine::instance()->isOpen(id))
		{
			return;
		}

		// Centred on the window in front (a wizard, a dialog), which the question is about. On the
		// heap: closing that window while the box is open deletes the box with it.
		QWidget* parent = QApplication::activeModalWidget();
		if (!parent)
		{
			parent = QApplication::activeWindow();
		}
		const QPointer<QtMessageBox> msgBox = new QtMessageBox(parent ? parent : m_mainWindow);
		msgBox->setWindowTitle(QStringLiteral("Sourcetrail"));
		msgBox->setIcon(options.size() > 1 ? QtMessageBox::Question : QtMessageBox::Information);
		msgBox->setText(QString::fromStdString(text));

		for (const std::string& option: options)
		{
			msgBox->addButton(QString::fromStdString(option), QtMessageBox::AcceptRole);
		}

		// The answer is on behalf of whoever clicked (an agent's posted click carries
		// its request, a person's has none: QtApplication::notify), not of what put the
		// box up.
		std::uint64_t clickCause = 0;
		QObject::connect(msgBox, &QMessageBox::buttonClicked, msgBox, [&clickCause](QAbstractButton *) {
			clickCause = currentCause();
		});

		m_questionBoxes[id] = msgBox;
		QAbstractButton *clickedButton = msgBox->execModal();
		m_questionBoxes.erase(id);

		int result = -1;
		if (msgBox)
		{
			result = static_cast<int>(msgBox->buttons().indexOf(clickedButton));
			delete msgBox;
		}

		// Does nothing when an agent's answer closed the box.
		const CauseScope cause(clickCause);
		QuestionStateMachine::instance()->answer(id, result, EventSource::Ui);
	});
}

void QtDialogView::dismissQuestion(std::uint64_t id)
{
	execution::qt::onUi(m_mainWindow, [=, this]() {
		auto it = m_questionBoxes.find(id);
		if (it != m_questionBoxes.end() && it->second)
		{
			it->second->done(0);
		}
	});
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
	// The app is closing: whoever waits on a question or a report this view shows
	// goes on (with no answer) instead of blocking the shutdown.
	QuestionStateMachine::instance()->withdraw(*this);
	IndexingStateMachine::instance()->detach(*this);
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
