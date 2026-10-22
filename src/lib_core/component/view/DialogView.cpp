#include "DialogView.h"

#include "Project.h"

DialogView::DialogView(UseCase useCase, StorageAccess* storageAccess)
	: m_useCase(useCase), m_storageAccess(storageAccess)
{
}

DialogView::~DialogView()
{
	QuestionStateMachine::instance()->withdraw(this);
	IndexingStateMachine::instance()->detach(this);
}

DialogView::UseCase DialogView::getUseCase() const
{
	return m_useCase;
}

void DialogView::setDialogsHideable(bool hideable)
{
	m_dialogsHideable = hideable;
}

bool DialogView::dialogsHidden() const
{
	return false;
}

void DialogView::clearDialogs() {}

void DialogView::showUnknownProgressDialog(const std::string&  /*title*/, const std::string&  /*message*/)
{
}

void DialogView::hideUnknownProgressDialog() {}

void DialogView::showProgressDialog(const std::string&  /*title*/, const std::string&  /*message*/, size_t  /*progress*/)
{
}

void DialogView::hideProgressDialog() {}

void DialogView::startIndexingDialog(
	Project* project,
	const std::vector<RefreshMode>& enabledModes,
	const RefreshMode initialMode,
	std::function<void(const RefreshInfo& info)> onStartIndexing,
	std::function<void()> onCancelIndexing)
{
	IndexingStateMachine::instance()->chooseMode(
		[project](RefreshMode mode) { return project->getRefreshInfo(mode); },
		enabledModes,
		initialMode,
		std::move(onStartIndexing),
		std::move(onCancelIndexing),
		this);
}

void DialogView::updateIndexingDialog(
	size_t startedFileCount,
	size_t finishedFileCount,
	size_t totalFileCount,
	const std::vector<FilePath>& sourcePaths)
{
	IndexingStateMachine::instance()->progress(
		startedFileCount, finishedFileCount, totalFileCount, sourcePaths.empty() ? "" : sourcePaths.back().str());
	showIndexingProgress(startedFileCount, finishedFileCount, totalFileCount, sourcePaths);
}

void DialogView::updateCustomIndexingDialog(
	size_t startedFileCount,
	size_t finishedFileCount,
	size_t totalFileCount,
	const std::vector<FilePath>& sourcePaths)
{
	IndexingStateMachine::instance()->progress(
		startedFileCount, finishedFileCount, totalFileCount, sourcePaths.empty() ? "" : sourcePaths.back().str());
	showCustomIndexingProgress(startedFileCount, finishedFileCount, totalFileCount, sourcePaths);
}

DatabasePolicy DialogView::finishedIndexingDialog(
	size_t indexedFileCount,
	size_t totalIndexedFileCount,
	size_t completedFileCount,
	size_t totalFileCount,
	float time,
	ErrorCountInfo errorInfo,
	bool interrupted)
{
	return IndexingStateMachine::instance()->report(
		{indexedFileCount,
		 totalIndexedFileCount,
		 completedFileCount,
		 totalFileCount,
		 time,
		 errorInfo.total,
		 errorInfo.fatal,
		 interrupted},
		this);
}

int DialogView::confirm(const std::string& message)
{
	return confirm(message, std::vector<std::string>());
}

int DialogView::confirm(const std::string& message, const std::vector<std::string>& options)
{
	if (!presentsQuestions())
	{
		return -1;
	}
	return QuestionStateMachine::instance()->ask(message, options, this);
}

bool DialogView::presentsQuestions() const
{
	return false;
}

void DialogView::showIndexingProgress(
	size_t  /*startedFileCount*/,
	size_t  /*finishedFileCount*/,
	size_t  /*totalFileCount*/,
	const std::vector<FilePath>&  /*sourcePaths*/)
{
}

void DialogView::showCustomIndexingProgress(
	size_t  /*startedFileCount*/,
	size_t  /*finishedFileCount*/,
	size_t  /*totalFileCount*/,
	const std::vector<FilePath>&  /*sourcePaths*/)
{
}

void DialogView::presentQuestion(
	std::uint64_t  /*id*/, const std::string&  /*text*/, const std::vector<std::string>&  /*options*/)
{
}

void DialogView::dismissQuestion(std::uint64_t  /*id*/) {}

void DialogView::presentModeChoice(const std::vector<RefreshMode>&  /*modes*/, RefreshMode  /*mode*/) {}

void DialogView::presentCounting() {}

void DialogView::presentRefreshInfo(const RefreshInfo&  /*info*/) {}

void DialogView::dismissModeChoice(bool  /*started*/) {}

bool DialogView::presentReport(const IndexingReport&  /*report*/)
{
	return false;	 // nobody to ask without a GUI: the run keeps its data
}

void DialogView::dismissReport() {}
