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
class DialogView
	: public QuestionPresenter
	, public IndexingPresenter
{
public:
	enum class UseCase
	{
		GENERAL,
		INDEXING,
		PROJECT_SETUP
	};

	DialogView(UseCase useCase, StorageAccess* storageAccess);
	~DialogView() override;

	UseCase getUseCase() const;

	void setDialogsHideable(bool hideable);

	virtual bool dialogsHidden() const;
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
		std::function<void()> onCancelIndexing);
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
	// Waits for the decision on the run's data.
	DatabasePolicy finishedIndexingDialog(
		size_t indexedFileCount,
		size_t totalIndexedFileCount,
		size_t completedFileCount,
		size_t totalFileCount,
		float time,
		ErrorCountInfo errorInfo,
		bool interrupted);

	// Asks and waits: the index of the chosen option, -1 for none.
	int confirm(const std::string& message);
	int confirm(const std::string& message, const std::vector<std::string>& options);
	// Asks without holding a thread: a sender of what confirm returns.
	QuestionSender asked(const std::string& message, const std::vector<std::string>& options);

protected:
	// Whether this view can show a question; without one, confirm() answers -1.
	virtual bool presentsQuestions() const;

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

	void presentQuestion(
		std::uint64_t id, const std::string& text, const std::vector<std::string>& options) override;
	void dismissQuestion(std::uint64_t id) override;

	void presentModeChoice(const std::vector<RefreshMode>& modes, RefreshMode mode) override;
	void presentCounting() override;
	void presentRefreshInfo(const RefreshInfo& info) override;
	void dismissModeChoice(bool started) override;
	bool presentReport(const IndexingReport& report) override;
	void dismissReport() override;

	const UseCase m_useCase;
	StorageAccess* m_storageAccess;

	bool m_dialogsHideable = false;
};

#endif	  // DIALOG_VIEW_H
