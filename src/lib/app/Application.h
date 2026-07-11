#ifndef APPLICATION_H
#define APPLICATION_H

#include <memory>

#include "DialogView.h"
#include "MessageActivateWindow.h"
#include "MessageCloseProject.h"
#include "MessageIndexingFinished.h"
#include "MessageListener.h"
#include "MessageLoadProject.h"
#include "MessageRefresh.h"
#include "MessageRefreshUI.h"
#include "MessageSwitchColorScheme.h"
#include "Project.h"

namespace execution
{
class ISchedulers;
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
	: public MessageListener<MessageActivateWindow>
	, public MessageListener<MessageCloseProject>
	, public MessageListener<MessageIndexingFinished>
	, public MessageListener<MessageLoadProject>
	, public MessageListener<MessageRefresh>
	, public MessageListener<MessageRefreshUI>
	, public MessageListener<MessageSwitchColorScheme>
{
public:
	static void createInstance(
		const Version& version,
		ViewFactory* viewFactory,
		NetworkFactory* networkFactory,
		execution::ISchedulers* schedulers = nullptr,
		bool enableAgentControl = false);
	static std::shared_ptr<Application> getInstance();
	static void destroyInstance();

	//! Process I/O / Compute / UI schedulers, injected at createInstance() (the
	//! GUI bootstrap passes execution::qt::Schedulers; null in headless/tests).
	execution::ISchedulers* getSchedulers() const;

	static std::string getUUID();

	static void loadSettings();
	static void loadStyle(const FilePath& colorSchemePath);

	~Application() override;

	std::shared_ptr<const Project> getCurrentProject() const;
	FilePath getCurrentProjectPath() const;
	bool isProjectLoaded() const;

	bool hasGUI();

	int handleDialog(const std::string& message);
	int handleDialog(const std::string& message, const std::vector<std::string>& options);
	std::shared_ptr<DialogView> getDialogView(DialogView::UseCase useCase);

	void updateHistoryMenu(std::shared_ptr<MessageBase> message);
	void updateBookmarks(const std::vector<std::shared_ptr<Bookmark>>& bookmarks);

private:
	static std::shared_ptr<Application> s_instance;
	static std::string s_uuid;

	Application(bool withGUI = true);

	void handleMessage(MessageActivateWindow* message) override;
	void handleMessage(MessageCloseProject* message) override;
	void handleMessage(MessageIndexingFinished* message) override;
	void handleMessage(MessageLoadProject* message) override;
	void handleMessage(MessageRefresh* message) override;
	void handleMessage(MessageRefreshUI* message) override;
	void handleMessage(MessageSwitchColorScheme* message) override;

	static void startMessagingAndScheduling();

	void loadWindow(bool showStartWindow);

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

	std::shared_ptr<Project> m_project;
	std::shared_ptr<StorageCache> m_storageCache;

	std::shared_ptr<MainView> m_mainView;

	std::shared_ptr<IDECommunicationController> m_ideCommunicationController;

	// Agent-UI control channel (thoth-ipc), created only when enabled at startup.
	// A no-op stub unless built with SOURCETRAIL_AGENT_CONTROL. See
	// context/DESIGN_AGENT_UI_CONTROL.md.
	std::shared_ptr<AgentControlController> m_agentControlController;
};

#endif	  // APPLICATION_H
