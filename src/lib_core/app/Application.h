#ifndef APPLICATION_H
#define APPLICATION_H

#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include "DialogView.h"
#include "MessageActivateErrors.h"
#include "MessageActivateOverview.h"
#include "MessageActivateTokens.h"
#include "MessageActivateWindow.h"
#include "MessageCloseProject.h"
#include "MessageIndexingFinished.h"
#include "MessageListener.h"
#include "MessageLoadProject.h"
#include "MessageRefresh.h"
#include "MessageRefreshUI.h"
#include "MessageSwitchColorScheme.h"
#include "MessageWindowClosed.h"
#include "Project.h"
#include "StdexecPrelude.h"

namespace execution
{
class ISchedulers;
class Strand;
}

class Bookmark;
class AgentControlController;
class IDECommunicationController;
class MainView;
class NetworkFactory;
class StorageCache;
class UpdateChecker;
class Version;
class ViewFactory;

class Application
	: public MessageListener<MessageActivateErrors>
	, public MessageListener<MessageActivateOverview>
	, public MessageListener<MessageActivateTokens>
	, public MessageListener<MessageActivateWindow>
	, public MessageListener<MessageCloseProject>
	, public MessageListener<MessageIndexingFinished>
	, public MessageListener<MessageLoadProject>
	, public MessageListener<MessageRefresh>
	, public MessageListener<MessageRefreshUI>
	, public MessageListener<MessageSwitchColorScheme>
	, public MessageListener<MessageWindowClosed>
{
public:
	static void createInstance(
		const Version& version,
		ViewFactory* viewFactory,
		NetworkFactory* networkFactory,
		execution::ISchedulers* schedulers = nullptr,
		bool enableAgentControl = false,
		const std::string& agentInstanceId = "");
	static std::shared_ptr<Application> getInstance();
	static void destroyInstance();

	//! Process I/O / Compute / UI schedulers, injected at createInstance() (the
	//! GUI bootstrap passes execution::qt::Schedulers; null in headless/tests).
	execution::ISchedulers* getSchedulers() const;

	static std::string getUUID();

	static void loadSettings();
	static void loadStyle(const FilePath& colorSchemePath);

	//! Installs a callback used to resolve the color scheme path for the initial
	//! and refresh style loads. The GUI layer installs a provider that honors the
	//! system Day/Night appearance (see QtColorSchemeWatcher); when no provider is
	//! set the stored ApplicationSettings color scheme is used.
	static void setColorSchemePathProvider(std::function<FilePath()> provider);

	~Application() override;

	std::shared_ptr<const Project> getCurrentProject() const;
	FilePath getCurrentProjectPath() const;
	bool isProjectLoaded() const;

	bool hasGUI();

	int handleDialog(const std::string& message);
	int handleDialog(const std::string& message, const std::vector<std::string>& options);
	// Shows a notice and goes on (DialogView::notify).
	void showNotice(const std::string& message, const std::vector<std::string>& options = {});
	std::shared_ptr<DialogView> getDialogView(DialogView::UseCase useCase);

	void updateHistoryMenu(std::shared_ptr<MessageBase> message);
	void updateBookmarks(const std::vector<std::shared_ptr<Bookmark>>& bookmarks);

private:
	static std::shared_ptr<Application> s_instance;
	static std::string s_uuid;
	static std::function<FilePath()> s_colorSchemePathProvider;

	//! Resolves the color scheme path via the installed provider, falling back to
	//! the stored ApplicationSettings color scheme.
	static FilePath resolveColorSchemePath();

	Application(bool withGUI = true);

	// The first activation of the tab a load opened ends the load (ProjectStateMachine).
	void handleMessage(MessageActivateErrors* message) override;
	void handleMessage(MessageActivateOverview* message) override;
	void handleMessage(MessageActivateTokens* message) override;
	void handleMessage(MessageActivateWindow* message) override;
	void handleMessage(MessageCloseProject* message) override;
	void handleMessage(MessageIndexingFinished* message) override;
	void handleMessage(MessageLoadProject* message) override;
	void handleMessage(MessageRefresh* message) override;
	void handleMessage(MessageRefreshUI* message) override;
	void handleMessage(MessageSwitchColorScheme* message) override;
	void handleMessage(MessageWindowClosed* message) override;

	void startMessagingAndScheduling();

	void loadWindow(bool showStartWindow);

	// Loads the project at `path`: with schedulers as a pipeline, where the index
	// opens on the I/O pool, a question holds no thread, and the project becomes the
	// app's back on the app strand; without them in a row on this thread.
	struct ProjectLoad;
	void loadProject(const FilePath& path, const ShardConfig& shardConfig, RefreshMode refreshMode);
	// On the app's thread: the project becomes the app's, unless a load asked for
	// since (or a close) dropped it.
	void endLoad(const std::shared_ptr<ProjectLoad>& load);

	// Asks the refresh's questions from `next` on, one after the other without
	// holding a thread, then refreshes on the app's thread, unless one cancelled it.
	void askThenRefresh(std::shared_ptr<Project> project, Project::RefreshPlan plan, std::size_t next, std::uint64_t cause);

	void refreshProject(RefreshMode refreshMode);
	void updateRecentProjects(const FilePath& projectSettingsFilePath);

	void logStorageStats() const;

	void updateTitle();

	bool checkSharedMemory();

	const bool m_hasGUI;
	bool m_loadedWindow = false;

	// Non-owning: the concrete schedulers are a process-wide singleton owned by
	// the GUI bootstrap; null in headless/test contexts.
	execution::ISchedulers* m_schedulers = nullptr;
	// The app scheduler's tasks (the message handlers among them) run here, over
	// the I/O pool; null without schedulers, where the scheduler has a thread.
	std::shared_ptr<execution::Strand> m_appStrand;

	std::shared_ptr<Project> m_project;
	std::shared_ptr<StorageCache> m_storageCache;
	// The load in flight, read and written on the app's thread only.
	std::shared_ptr<ProjectLoad> m_loading;
	// The pipelines of loads and refreshes, which stop with the app.
	exec::async_scope m_pipelines;
	// The app is stopping them: a question they asked was withdrawn, not answered,
	// so they act on none of its account.
	std::atomic<bool> m_pipelinesStopping = false;
	// Whether they are stopping, or the app is closing: a question box the closing
	// ended answers "none", which is not a person's answer either.
	bool pipelinesStopping() const;

	std::shared_ptr<MainView> m_mainView;

	std::shared_ptr<IDECommunicationController> m_ideCommunicationController;

	// Agent-UI control channel (thoth-ipc), created only when enabled at startup.
	// A no-op stub unless built with SOURCETRAIL_AGENT_CONTROL. See
	// context/DESIGN_AGENT_UI_CONTROL.md.
	std::shared_ptr<AgentControlController> m_agentControlController;
};

#endif	  // APPLICATION_H
