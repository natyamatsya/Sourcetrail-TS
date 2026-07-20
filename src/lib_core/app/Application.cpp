#include "Application.h"

#include "CppSQLite3.h"

#include "ApplicationSettings.h"
#include "ColorScheme.h"
#include "DialogView.h"
#include "FileLogger.h"
#include "GraphViewStyle.h"
#include "AgentControlController.h"
#include "IDECommunicationController.h"
#include "LogManager.h"
#include "MainView.h"
#include "MessageFilterErrorCountUpdate.h"
#include "MessageFilterFocusInOut.h"
#include "MessageFilterSearchAutocomplete.h"
#include "MessageQueue.h"
#include "MessageQuitApplication.h"
#include "MessageStatus.h"
#include "MessageTextEncodingChanged.h"
#include "NetworkFactory.h"
#include "ProjectSettings.h"
#include "InterprocessBackend.h"
#include "StorageCache.h"
#include "TabIds.h"
#include "TaskManager.h"
#include "UserPaths.h"
#include "Version.h"
#include "ViewFactory.h"
#include "logging.h"
#include "tracing.h"
#include "utilityUuid.h"

std::shared_ptr<Application> Application::s_instance;
std::function<FilePath()> Application::s_colorSchemePathProvider;
std::string Application::s_uuid;

void Application::createInstance(
	const Version& version,
	ViewFactory* viewFactory,
	NetworkFactory* networkFactory,
	execution::ISchedulers* schedulers,
	bool enableAgentControl,
	const std::string& agentInstanceId)
{
	bool hasGui = (viewFactory != nullptr);

	Version::setApplicationVersion(version);

	if (hasGui)
	{
		GraphViewStyle::setImpl(viewFactory->createGraphStyleImpl());
	}

	loadSettings();

	GarbageCollectorImpl* collector = GarbageCollectorImpl::createInstance();
	if (collector != nullptr)
	{
		collector->run(Application::getUUID());
	}

	TaskManager::createScheduler(TabIds::app());
	TaskManager::createScheduler(TabIds::background());
	MessageQueue::getInstance();

	s_instance = std::shared_ptr<Application>(new Application(hasGui));

	s_instance->m_schedulers = schedulers;

	s_instance->m_storageCache = std::make_shared<StorageCache>();

	if (hasGui)
	{
		s_instance->m_mainView = viewFactory->createMainView(s_instance->m_storageCache.get());
		s_instance->m_mainView->setup();
	}

	if (networkFactory != nullptr)
	{
		s_instance->m_ideCommunicationController = networkFactory->createIDECommunicationController(
			s_instance->m_storageCache.get());
		s_instance->m_ideCommunicationController->startListening();
	}

	// Agent-UI control channel: needs the GUI (live views/state) and the
	// schedulers (the ui() hop). No-op stub unless built with SOURCETRAIL_AGENT_CONTROL.
	if (enableAgentControl && hasGui && s_instance->m_schedulers != nullptr)
	{
		s_instance->m_agentControlController = std::make_shared<AgentControlController>(
			s_instance->m_storageCache.get(), s_instance->m_schedulers, agentInstanceId);
		s_instance->m_agentControlController->startListening();
	}

	s_instance->startMessagingAndScheduling();
}

std::shared_ptr<Application> Application::getInstance()
{
	return s_instance;
}

execution::ISchedulers* Application::getSchedulers() const
{
	return m_schedulers;
}

void Application::destroyInstance()
{
	// Stop the agent reader thread before the message loop / schedulers go away.
	if (s_instance && s_instance->m_agentControlController)
	{
		s_instance->m_agentControlController->stopListening();
	}

	MessageQueue::getInstance()->stopMessageLoop();
	TaskManager::destroyScheduler(TabIds::background());
	TaskManager::destroyScheduler(TabIds::app());

	if (GarbageCollectorImpl* gc = GarbageCollectorImpl::getInstance())
	{
		gc->stop();
		// Destroy NOW, while libipc's per-process statics are still alive. The
		// collector owns an IpcSharedMemory by value; letting the static
		// singleton die at __cxa_finalize closes its mutex against libipc's
		// already-destroyed handle cache (observed: SIGSEGV in
		// ipc::shm::handle::name() at exit).
		GarbageCollectorImpl::destroyInstance();
	}

	s_instance.reset();
}

std::string Application::getUUID()
{
	if (s_uuid.size() == 0)
	{
		s_uuid = utility::getUuidString();
	}

	return s_uuid;
}

void Application::loadSettings()
{
	MessageStatus("Load settings: " + UserPaths::getAppSettingsFilePath().str()).dispatch();

	std::shared_ptr<ApplicationSettings> settings = ApplicationSettings::getInstance();
	const FilePath settingsPath = UserPaths::getAppSettingsFilePath();
	const bool migratedFromXml = !settingsPath.exists();
	settings->load(settingsPath);
	if (migratedFromXml && settings->getFilePath() == settingsPath)
	{
		// Materialize the migrated settings in the new (JSON) format now, so the app
		// no longer depends on the legacy XML (which is kept as a backup).
		LOG_INFO("Migrated settings to " + settingsPath.str());
		settings->save();
	}
	MessageTextEncodingChanged(settings->getTextEncoding()).dispatch();

	LogManager::getInstance()->setLoggingEnabled(settings->getLoggingEnabled());
	Logger* logger = LogManager::getInstance()->getLoggerByType("FileLogger");
	if (logger != nullptr)
	{
		auto *fileLogger = dynamic_cast<FileLogger*>(logger);
		fileLogger->setLogDirectory(settings->getLogDirectoryPath().getPath());
		fileLogger->setFileName(FileLogger::generateDatedFileName("log"));
	}

	loadStyle(resolveColorSchemePath());
}

void Application::loadStyle(const FilePath& colorSchemePath)
{
	LOG_INFO("Loading color scheme: " + colorSchemePath.str());
	ColorScheme::getInstance()->load(colorSchemePath);
	GraphViewStyle::loadStyleSettings();
}

void Application::setColorSchemePathProvider(std::function<FilePath()> provider)
{
	s_colorSchemePathProvider = std::move(provider);
}

FilePath Application::resolveColorSchemePath()
{
	if (s_colorSchemePathProvider)
	{
		return s_colorSchemePathProvider();
	}
	return ApplicationSettings::getInstance()->getColorSchemePath();
}

Application::Application(bool withGUI): m_hasGUI(withGUI) {}

Application::~Application()
{
	if (m_hasGUI)
	{
		m_mainView->saveLayout();
	}
}

std::shared_ptr<const Project> Application::getCurrentProject() const
{
	return m_project;
}

FilePath Application::getCurrentProjectPath() const
{
	if (m_project)
	{
		return m_project->getProjectSettingsFilePath();
	}

	return FilePath();
}

bool Application::isProjectLoaded() const
{
	if (m_project)
	{
		return m_project->isLoaded();
	}
	return false;
}

bool Application::hasGUI()
{
	return m_hasGUI;
}

int Application::handleDialog(const std::string& message)
{
	return getDialogView(DialogView::UseCase::GENERAL)->confirm(message);
}

int Application::handleDialog(const std::string& message, const std::vector<std::string>& options)
{
	return getDialogView(DialogView::UseCase::GENERAL)->confirm(message, options);
}

std::shared_ptr<DialogView> Application::getDialogView(DialogView::UseCase useCase)
{
	if (m_mainView)
	{
		return m_mainView->getDialogView(useCase);
	}

	return std::make_shared<DialogView>(useCase, nullptr);
}

void Application::updateHistoryMenu(std::shared_ptr<MessageBase> message)
{
	m_mainView->updateHistoryMenu(message);
}

void Application::updateBookmarks(const std::vector<std::shared_ptr<Bookmark>>& bookmarks)
{
	m_mainView->updateBookmarksMenu(bookmarks);
}

void Application::handleMessage(MessageActivateWindow*  /*message*/)
{
	if (m_hasGUI)
	{
		m_mainView->activateWindow();
	}
}

void Application::handleMessage(MessageCloseProject*  /*message*/)
{
	if (m_project && m_project->isIndexing())
	{
		MessageStatus("Cannot close the project while indexing.", true, false).dispatch();
		return;
	}

	m_project.reset();
	updateTitle();
	m_mainView->clear();
}

void Application::handleMessage(MessageIndexingFinished*  /*message*/)
{
	logStorageStats();

	if (m_hasGUI)
	{
		MessageRefreshUI().afterIndexing().dispatch();
	}
	else
	{
		MessageQuitApplication().dispatch();
	}
}

void Application::handleMessage(MessageLoadProject* message)
{
	TRACE("app load project");

	FilePath projectSettingsFilePath(message->projectSettingsFilePath);
	loadWindow(projectSettingsFilePath.empty());

	if (projectSettingsFilePath.empty())
	{
		return;
	}

	if (m_project && m_project->isIndexing())
	{
		MessageStatus("Cannot load another project while indexing.", true, false).dispatch();
		return;
	}

	if (m_project && projectSettingsFilePath == m_project->getProjectSettingsFilePath())
	{
		if (message->settingsChanged && m_hasGUI)
		{
			m_project->setStateOutdated();
			refreshProject(RefreshMode::ALL_FILES);
		}
	}
	else
	{
		MessageStatus("Loading Project: " + projectSettingsFilePath.str(), false, true).dispatch();

		m_project.reset();

		if (m_hasGUI)
		{
			m_mainView->clear();
		}

		try
		{
			updateRecentProjects(projectSettingsFilePath);

			m_storageCache->clear();
			m_storageCache->setSubject(
				std::weak_ptr<StorageAccess>());	// TODO: check if this is really required.

			m_project = std::make_shared<Project>(
				std::make_shared<ProjectSettings>(projectSettingsFilePath),
				m_storageCache.get(),
				getUUID(),
				hasGUI());

			if (m_project)
			{
				m_project->setShardConfig(message->shardConfig);
				m_project->load(getDialogView(DialogView::UseCase::GENERAL));
			}
			else
			{
				LOG_ERROR_STREAM(<< "Failed to load project.");
				MessageStatus("Failed to load project: " + projectSettingsFilePath.str(), true)
					.dispatch();
			}

			updateTitle();
		}
		catch (CppSQLite3Exception& e)
		{
			const std::string message = "Failed to load project at \"" +
										 projectSettingsFilePath.str() + "\" with sqlite exception: " +
										 e.errorMessage();
			LOG_ERROR(message);
			MessageStatus(message, true).dispatch();
		}
		catch (std::exception& e)
		{
			const std::string message = "Failed to load project at \"" +
				projectSettingsFilePath.str() + "\" with exception: " +
				e.what();
			LOG_ERROR(message);
			MessageStatus(message, true).dispatch();
		}
		catch (...)
		{
			const std::string message = "Failed to load project at \"" +
				projectSettingsFilePath.str() + "\" with unknown exception.";
			LOG_ERROR(message);
			MessageStatus(message, true).dispatch();
		}

		if (message->refreshMode != RefreshMode::NONE)
		{
			refreshProject(message->refreshMode);
		}
	}
}

void Application::handleMessage(MessageRefresh* message)
{
	TRACE("app refresh");

	refreshProject(message->all ? RefreshMode::ALL_FILES : RefreshMode::UPDATED_FILES);
}

void Application::handleMessage(MessageRefreshUI* message)
{
	TRACE("ui refresh");

	if (m_hasGUI)
	{
		updateTitle();

		if (message->loadStyle)
		{
			loadStyle(resolveColorSchemePath());
		}

		m_mainView->refreshViews();

		m_mainView->refreshUIState(message->isAfterIndexing);
	}
}

void Application::handleMessage(MessageSwitchColorScheme* message)
{
	MessageStatus("Switch color scheme: " + message->colorSchemePath.str()).dispatch();

	loadStyle(message->colorSchemePath);
	MessageRefreshUI().noStyleReload().dispatch();
}

void Application::startMessagingAndScheduling()
{
	TaskManager::getScheduler(TabIds::app())->startSchedulerLoopThreaded();
	TaskManager::getScheduler(TabIds::background())->startSchedulerLoopThreaded();

	MessageQueue* queue = MessageQueue::getInstance().get();
	queue->addMessageFilter(std::make_shared<MessageFilterErrorCountUpdate>());
	queue->addMessageFilter(std::make_shared<MessageFilterFocusInOut>());
	queue->addMessageFilter(std::make_shared<MessageFilterSearchAutocomplete>());

	queue->setSendMessagesAsTasks(true);
	queue->startMessageLoopThreaded();
}

void Application::loadWindow(bool showStartWindow)
{
	if (!m_hasGUI)
	{
		return;
	}

	if (!m_loadedWindow)
	{
		updateTitle();

		m_mainView->loadWindow(showStartWindow);
		m_loadedWindow = true;
	}
	else if (!showStartWindow)
	{
		m_mainView->hideStartScreen();
	}
}

void Application::refreshProject(RefreshMode refreshMode)
{
	if (m_project && checkSharedMemory())
	{
		m_project->refresh(getDialogView(DialogView::UseCase::INDEXING), refreshMode);

		if (!m_hasGUI && !m_project->isIndexing())
		{
			MessageQuitApplication().dispatch();
		}
	}
}

void Application::updateRecentProjects(const FilePath& projectSettingsFilePath)
{
	if (m_hasGUI)
	{
		ApplicationSettings* appSettings = ApplicationSettings::getInstance().get();
		std::vector<FilePath> recentProjects = appSettings->getRecentProjects();
		if (recentProjects.size() != 0)
		{
			std::vector<FilePath>::iterator it = std::find(
				recentProjects.begin(), recentProjects.end(), projectSettingsFilePath);
			if (it != recentProjects.end())
			{
				recentProjects.erase(it);
			}
		}

		recentProjects.insert(recentProjects.begin(), projectSettingsFilePath);
		while (recentProjects.size() > ApplicationSettings::getMaxRecentProjectsCount())
		{
			recentProjects.pop_back();
		}

		appSettings->setRecentProjects(recentProjects);
		appSettings->save(UserPaths::getAppSettingsFilePath());

		m_mainView->updateRecentProjectMenu();
	}
}

void Application::logStorageStats() const
{
	if (!ApplicationSettings::getInstance()->getLoggingEnabled())
	{
		return;
	}

	std::stringstream ss;
	StorageStats stats = m_storageCache->getStorageStats();

	ss << "\nGraph:\n";
	ss << "\t" << stats.nodeCount << " Nodes\n";
	ss << "\t" << stats.edgeCount << " Edges\n";

	ss << "\nCode:\n";
	ss << "\t" << stats.fileCount << " Files\n";
	ss << "\t" << stats.fileLOCCount << " Lines of Code\n";


	ErrorCountInfo errorCount = m_storageCache->getErrorCount();

	ss << "\nErrors:\n";
	ss << "\t" << errorCount.total << " Errors\n";
	ss << "\t" << errorCount.fatal << " Fatal Errors\n";

	LOG_INFO(ss.str());
}

void Application::updateTitle()
{
	if (m_hasGUI)
	{
		std::string title = "Sourcetrail";

		if (m_project)
		{
			FilePath projectPath = m_project->getProjectSettingsFilePath();

			if (!projectPath.empty())
			{
				title += " - " + projectPath.fileName();
			}
		}

		m_mainView->setTitle(title);
	}
}

bool Application::checkSharedMemory()
{
	return true;
}
