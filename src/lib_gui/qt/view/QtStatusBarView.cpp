#include "QtStatusBarView.h"

#include <QStatusBar>

#include "QtMainView.h"
#include "UiPost.h"

QtStatusBarView::QtStatusBarView(ViewLayout* viewLayout): StatusBarView(viewLayout)
{
	m_statusBar = new QtStatusBar();
	m_statusBar->show();

	dynamic_cast<QtMainView*>(viewLayout)->setStatusBar(m_statusBar);
}

void QtStatusBarView::createWidgetWrapper() {}

void QtStatusBarView::refreshView() {}

void QtStatusBarView::showMessage(const std::string& message, bool isError, bool showLoader)
{
	execution::qt::onUi(m_statusBar, [=, this]() { m_statusBar->setText(message, isError, showLoader); });
}

void QtStatusBarView::setErrorCount(ErrorCountInfo errorCount)
{
	execution::qt::onUi(m_statusBar, [=, this]() { m_statusBar->setErrorCount(errorCount); });
}

void QtStatusBarView::showIdeStatus(const std::string& message)
{
	execution::qt::onUi(m_statusBar, [=, this]() { m_statusBar->setIdeStatus(message); });
}

void QtStatusBarView::showIndexingProgress(size_t progressPercent)
{
	execution::qt::onUi(m_statusBar, [=, this]() { m_statusBar->showIndexingProgress(progressPercent); });
}

void QtStatusBarView::hideIndexingProgress()
{
	execution::qt::onUi(m_statusBar, [=, this]() { m_statusBar->hideIndexingProgress(); });
}

void QtStatusBarView::showTextEncoding(const std::string &encoding)
{
	execution::qt::onUi(m_statusBar, [=, this]() { m_statusBar->setTextEncoding(encoding); });
}
