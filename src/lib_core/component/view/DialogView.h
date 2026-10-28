#ifndef DIALOG_VIEW_H
#define DIALOG_VIEW_H

#include <functional>
#include <string>
#include <vector>

#include "ErrorCountInfo.h"
#include "IndexingStateMachine.h"
#include "QuestionStateMachine.h"
#include "RefreshInfo.h"

class Project;
class StorageAccess;

// Where the application meets the person in front of it: questions, the indexing
// dialogs, progress. The questions and the indexing dialogs are states of the
// QuestionStateMachine and the IndexingStateMachine: the public methods move those
// machines, and a subclass shows their states through the presenter hooks (the
// base, without a GUI, shows nothing and answers for itself). Since the machines
// own the waiting, an agent answers through them just as the dialog's buttons do.
//
// The machines keep it as a QuestionPresenter and an IndexingPresenter: handles to
// whatever has the hooks, not bases it derives from.
class DialogView
{
public:
	enum class UseCase
	{
		GENERAL,
		INDEXING,
		PROJECT_SETUP
	};

	DialogView(UseCase useCase, StorageAccess* storageAccess);
	virtual ~DialogView();

	UseCase getUseCase() const;

	void setDialogsHideable(bool hideable);

	virtual bool dialogsHidden() const;
	// Whether the next dialogs show (a run behind the status bar's progress shows none
	// until it is asked for).
	virtual void setDialogsVisible(bool visible);
	virtual void clearDialogs();

	virtual void showUnknownProgressDialog(const std::string& title, const std::string& message);
	virtual void hideUnknownProgressDialog();

	virtual void showProgressDialog(
		const std::string& title, const std::string& message, size_t progress);
	virtual void hideProgressDialog();

	void startIndexingDialog(
		Project* project,
		const std::vector<RefreshMode>& enabledModes,
		const RefreshMode initialMode,
		std::function<void(const RefreshInfo& info)> onStartIndexing,
		std::function<void()> onCancelIndexing,
		const std::string& reason = {});
	void updateIndexingDialog(
		size_t startedFileCount,
		size_t finishedFileCount,
		size_t totalFileCount,
		const std::vector<FilePath>& sourcePaths);
	void updateCustomIndexingDialog(
		size_t startedFileCount,
		size_t finishedFileCount,
		size_t totalFileCount,
		const std::vector<FilePath>& sourcePaths);
	// Shows the report on the run without waiting: `decided` gets the decision on
	// its data, on the thread that decides.
	void finishedIndexingDialog(
		size_t indexedFileCount,
		size_t totalIndexedFileCount,
		size_t completedFileCount,
		size_t totalFileCount,
		float time,
		ErrorCountInfo errorInfo,
		bool interrupted,
		std::function<void(DatabasePolicy)> decided);

	// Asks and waits: the index of the chosen option, -1 for none.
	int confirm(const std::string& message);
	int confirm(const std::string& message, const std::vector<std::string>& options);
	// Asks without holding a thread: a sender of what confirm returns.
	QuestionSender asked(const std::string& message, const std::vector<std::string>& options);
	// Shows a notice and goes on: nothing waits for it to be closed.
	void notify(const std::string& message, const std::vector<std::string>& options = {});

	// The presenter hooks: what the machines call to show their states
	// (PresentsQuestions, PresentsIndexing).
	virtual void presentQuestion(std::uint64_t id, const std::string& text, const std::vector<std::string>& options);
	virtual void dismissQuestion(std::uint64_t id);

	virtual void presentModeChoice(const std::vector<RefreshMode>& modes, RefreshMode mode, const std::string& reason);
	virtual void presentCounting();
	virtual void presentRefreshInfo(const RefreshInfo& info);
	virtual void dismissModeChoice(bool started);
	virtual bool presentReport(const IndexingReport& report);
	virtual void dismissReport();

protected:
	// Whether this view can show a question; without one, confirm() answers -1.
	virtual bool presentsQuestions() const;
	// This view, if it presentsQuestions().
	QuestionPresenter questionPresenter();

	virtual void showIndexingProgress(
		size_t startedFileCount,
		size_t finishedFileCount,
		size_t totalFileCount,
		const std::vector<FilePath>& sourcePaths);
	virtual void showCustomIndexingProgress(
		size_t startedFileCount,
		size_t finishedFileCount,
		size_t totalFileCount,
		const std::vector<FilePath>& sourcePaths);

	const UseCase m_useCase;
	StorageAccess* m_storageAccess;

	bool m_dialogsHideable = false;
};

#endif	  // DIALOG_VIEW_H
