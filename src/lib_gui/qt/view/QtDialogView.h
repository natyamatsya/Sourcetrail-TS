#ifndef QT_DIALOG_VIEW_H
#define QT_DIALOG_VIEW_H

#include <map>

#include <QPointer>

#include "DialogView.h"

#include "QtWindowStack.h"

#include "MessageErrorCountUpdate.h"
#include "MessageIndexingShowDialog.h"
#include "MessageListener.h"
#include "MessageWindowClosed.h"

class QTimer;
class QtIndexingStartDialog;
class QtMainWindow;
class QtMessageBox;
class QtWindow;

class QtDialogView
	: public QObject
	, public DialogView
	, public MessageListener<MessageErrorCountUpdate>
	, public MessageListener<MessageIndexingShowDialog>
	, public MessageListener<MessageWindowClosed>
{
	Q_OBJECT

public:
	QtDialogView(QtMainWindow* mainWindow, UseCase useCase, StorageAccess* storageAccess);
	~QtDialogView() override;

	bool dialogsHidden() const override;
	void clearDialogs() override;

	void showUnknownProgressDialog(const std::string& title, const std::string& message) override;
	void hideUnknownProgressDialog() override;

	void showProgressDialog(
		const std::string& title, const std::string& message, size_t progress) override;
	void hideProgressDialog() override;

	void setParentWindow(QtWindow* window);

protected:
	bool presentsQuestions() const override;

	void showIndexingProgress(
		size_t startedFileCount,
		size_t finishedFileCount,
		size_t totalFileCount,
		const std::vector<FilePath>& sourcePaths) override;
	void showCustomIndexingProgress(
		size_t startedFileCount,
		size_t finishedFileCount,
		size_t totalFileCount,
		const std::vector<FilePath>& sourcePaths) override;

	void presentQuestion(
		std::uint64_t id, const std::string& text, const std::vector<std::string>& options) override;
	void dismissQuestion(std::uint64_t id) override;

	void presentModeChoice(const std::vector<RefreshMode>& modes, RefreshMode mode) override;
	void presentCounting() override;
	void presentRefreshInfo(const RefreshInfo& info) override;
	void dismissModeChoice(bool started) override;
	bool presentReport(const IndexingReport& report) override;
	void dismissReport() override;

private slots:
	void showUnknownProgress(const std::string& title, const std::string& message, bool stacked);
	void hideUnknownProgress();

	void setUIBlocked(bool blocked);
	void dialogVisibilityChanged(bool visible);

private:
	void handleMessage(MessageErrorCountUpdate* message) override;
	void handleMessage(MessageIndexingShowDialog* message) override;
	void handleMessage(MessageWindowClosed* message) override;

	void updateErrorCount(size_t errorCount, size_t fatalCount);
	// Stops the "Processing Files" indicator of the mode choice, or hides it once shown.
	void stopCounting();

	template <typename DialogType, typename... ParamTypes>
	DialogType* createWindow(ParamTypes... params);

	QtMainWindow* m_mainWindow;
	QtWindow* m_parentWindow = nullptr;

	QtWindowStack m_windowStack;

	// GUI thread only.
	QPointer<QtIndexingStartDialog> m_startDialog;
	QTimer* m_countingTimer = nullptr;	// shows "Processing Files" when counting takes a while
	std::map<std::uint64_t, QPointer<QtMessageBox>> m_questionBoxes;	// open questions by id

	bool m_uiBlocked = false;
	bool m_dialogsVisible = true;
};

#endif	  // QT_DIALOG_VIEW_H
