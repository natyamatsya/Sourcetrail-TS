#include "Application.h"

#include "CppSQLite3.h"

#include <optional>
#include <type_traits>
#include <format>

#ifndef SRCTRL_MODULE_BUILD
#include "ApplicationSettings.h"
#include "ColorScheme.h"
#endif
#include "DialogView.h"
#ifndef SRCTRL_MODULE_BUILD
#include "FileLogger.h"
#include "GraphViewStyle.h"
#endif
#include "AgentControlController.h"
#include "IDECommunicationController.h"
#include "ActivationStateMachine.h"
#include "AppStateMachine.h"
#include "IndexingStateMachine.h"
#include "ProjectStateMachine.h"
#include "ProjectWizardStateMachine.h"
#include "BookmarkStateMachine.h"
#include "QuestionStateMachine.h"
#include "SearchStateMachine.h"
#include "RemoteStateMachine.h"
#ifndef SRCTRL_MODULE_BUILD
#include "LogManager.h"
#endif
#include "MainView.h"
// classic headers (entities not exported): stay textual
#include "MessageCause.h"
#include "MessageQueue.h"
#ifndef SRCTRL_MODULE_BUILD
#include "MessageFilterErrorCountUpdate.h"
#include "MessageFilterFocusInOut.h"
#include "MessageFilterSearchAutocomplete.h"
#include "MessageQuitApplication.h"
#include "MessageStatus.h"
#include "MessageTextEncodingChanged.h"
#endif
#include "NetworkFactory.h"
#ifndef SRCTRL_MODULE_BUILD
#include "ProjectSettings.h"
#include "InterprocessBackend.h"
#endif
#include "StorageCache.h"
#include "TabIds.h"
#include "TabQueues.h"
#include "WorkPools.h"
#include "ISchedulers.h"
#include "Strand.h"
#ifndef SRCTRL_MODULE_BUILD
#include "UserPaths.h"
#endif
#include "Version.h"
#include "ViewFactory.h"
#include "logging.h"
#include "tracing.h"
#include "utilityUuid.h"

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.file;
import srctrl.interprocess;
import srctrl.logging;
import srctrl.messaging;
import srctrl.settings;
import srctrl.view;
#endif

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

	MessageQueue::getInstance();

	s_instance = std::shared_ptr<Application>(new Application(hasGui));

	s_instance->m_schedulers = schedulers;

	s_instance->m_storageCache = std::make_shared<StorageCache>();

	// The state machines remote control can drive (agent control's GetMachines).
	// The app machine is in `starting` until main reports it started.
	StateMachineRegistry::instance().add(AppStateMachine::instance());
	StateMachineRegistry::instance().add(ProjectStateMachine::instance());
	StateMachineRegistry::instance().add(IndexingStateMachine::instance());
	StateMachineRegistry::instance().add(QuestionStateMachine::instance());
	StateMachineRegistry::instance().add(ActivationStateMachine::instance());
	StateMachineRegistry::instance().add(SearchStateMachine::instance());
	StateMachineRegistry::instance().add(BookmarkStateMachine::instance());
	StateMachineRegistry::instance().add(ProjectWizardStateMachine::instance());
	// The activation machine follows what changes the views, which report when
	// they have drawn it: with no views there is nothing to follow.
	if (hasGui)
	{
		ActivationStateMachine::instance()->followMessages();
	}

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
	// Open from here on: an agent sees the app start (machine "app"), and a load it
	// asks for meanwhile waits for the start (ProjectStateMachine::load).
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

	// A load in flight ends here: its question is withdrawn, and what it opened is
	// not installed. No pipeline is spawned from here on.
	if (s_instance)
	{
		s_instance->m_pipelinesStopping = true;
		s_instance->m_pipelines.request_stop();
	}

	MessageQueue::getInstance()->stopMessageLoop();
	// What is queued to the app, the background and the tabs is dropped.
	execution::stopQueues();
	// An indexing run ends here (it reports how), before the pools it runs on go.
	if (s_instance && s_instance->m_project)
	{
		s_instance->m_project->stopIndexing();
	}
	execution::setWorkPool(std::nullopt);	// the app's pools go with it

	// What spawns pipelines (the message handlers, the tasks) has stopped: wait for
	// the ones in flight.
	if (s_instance)
	{
		stdexec::sync_wait(s_instance->m_pipelines.on_empty());
	}

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

void Application::showNotice(const std::string& message, const std::vector<std::string>& options)
{
	getDialogView(DialogView::UseCase::GENERAL)->notify(message, options);
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

template <typename Sender>
void Application::spawnPipeline(Sender pipeline)
{
	if (!m_pipelinesStopping)
	{
		m_pipelines.spawn(std::move(pipeline));
	}
}

struct Application::ProjectLoad
{
	FilePath path;
	std::uint64_t cause = 0;	// what asked for the load: all its steps are on its behalf
	RefreshMode refreshMode = RefreshMode::NONE;
	std::shared_ptr<Project> project;
	bool ended = false;	// a step ended the load before its index opened
	bool opened = false;	// the pipeline opened the index (finishLoad is left to do)
	std::string failure;	// what an exception said

	// Requested when a load or a close replaces this one, or the app stops its
	// pipelines (linked below): the pipeline runs under it, so its question is
	// withdrawn, and no step changes files after it.
	stdexec::inplace_stop_source stop;
	struct RequestStop
	{
		stdexec::inplace_stop_source* source;
		void operator()() const noexcept
		{
			source->request_stop();
		}
	};
	std::optional<stdexec::inplace_stop_callback<RequestStop>> stopWithApp;
	// The load counts as its request's work until it ends, its question included.
	std::optional<CauseWork::Held> work;

	// Runs a step (one returning false ends the load), turning what it throws into
	// the load's failure, as the load always has.
	template <typename Step>
	bool step(Step step) noexcept;

	// On the I/O pool: reads the settings; what an interrupted run leaves to ask.
	std::optional<Project::Question> readSettings() noexcept;
	// On the I/O pool with the answer: keeps or discards the interrupted run's data
	// and opens the index; what to tell when the index is in use. With `appClosing`,
	// or once the load was stopped, the answer is not a person's, and nothing changes.
	std::optional<Project::Question> openIndex(int option, bool appClosing) noexcept;
};

template <typename Step>
bool Application::ProjectLoad::step(Step step) noexcept
{
	if (ended)
	{
		return false;
	}
	try
	{
		if constexpr (std::is_void_v<decltype(step())>)
		{
			step();
		}
		else if (!step())
		{
			ended = true;
		}
	}
	catch (CppSQLite3Exception& e)
	{
		failure = "Failed to load project at \"" + path.str() + "\" with sqlite exception: " + e.errorMessage();
		ended = true;
	}
	catch (std::exception& e)
	{
		failure = "Failed to load project at \"" + path.str() + "\" with exception: " + e.what();
		ended = true;
	}
	catch (...)
	{
		failure = "Failed to load project at \"" + path.str() + "\" with unknown exception.";
		ended = true;
	}
	return !ended;
}

std::optional<Project::Question> Application::ProjectLoad::readSettings() noexcept
{
	const CauseScope scope(cause);
	if (!step([this]() { return project->readSettings(); }))
	{
		return std::nullopt;
	}
	return project->interruptedRunQuestion();
}

std::optional<Project::Question> Application::ProjectLoad::openIndex(int option, bool appClosing) noexcept
{
	const CauseScope scope(cause);
	if (appClosing || stop.stop_requested())
	{
		ended = true;	// keep or discard is for a person to say, not for the exit
		return std::nullopt;
	}
	bool inUse = false;
	if (!step([&]() {
			inUse = !project->resolveInterruptedRun(option);
			return !inUse;
		}))
	{
		return inUse ? std::optional(Project::indexInUse()) : std::nullopt;
	}
	opened = step([this]() { project->openIndex(); });
	return std::nullopt;
}

void Application::handleMessage(MessageCloseProject*  /*message*/)
{
	if (m_project && m_project->isIndexing())
	{
		MessageStatus("Cannot close the project while indexing.", true, false).dispatch();
		ProjectStateMachine::instance()->closeRefused("cannot close the project while indexing");
		return;
	}

	m_project.reset();
	if (m_loading)
	{
		m_loading->stop.request_stop();	// a load in flight withdraws its question and ends
		m_loading.reset();
	}
	updateTitle();
	m_mainView->clear();
	ProjectStateMachine::instance()->closed();
}

void Application::handleMessage(MessageActivateErrors*  /*message*/)
{
	ProjectStateMachine::instance()->shown();
}

void Application::handleMessage(MessageActivateOverview*  /*message*/)
{
	ProjectStateMachine::instance()->shown();
}

void Application::handleMessage(MessageActivateTokens*  /*message*/)
{
	ProjectStateMachine::instance()->shown();
}

void Application::handleMessage(MessageWindowClosed*  /*message*/)
{
	AppStateMachine::instance()->quitting();
}

void Application::handleMessage(MessageIndexingFinished* message)
{
	if (!message->outcome)
	{
		LOG_ERROR(
			"Indexing ended abnormally: " + utility::expectedErrorToString(message->outcome.error()));
	}

	logStorageStats();

	if (m_hasGUI)
	{
		MessageRefreshUI().afterIndexing().dispatch();
	}
	else
	{
		// The indexing outcome becomes the headless process exit code (0 = success), so scripts
		// and CI can trust the run instead of reading silence as success.
		MessageQuitApplication(message->outcome ? 0 : 1).dispatch();
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

	const std::shared_ptr<ProjectStateMachine> projectMachine = ProjectStateMachine::instance();

	if (m_project && m_project->isIndexing())
	{
		MessageStatus("Cannot load another project while indexing.", true, false).dispatch();
		projectMachine->kept("cannot load another project while indexing");
		return;
	}

	if (m_loading && projectSettingsFilePath == m_loading->path)
	{
		projectMachine->kept("the project is loading already");
	}
	else if (m_project && projectSettingsFilePath == m_project->getProjectSettingsFilePath())
	{
		projectMachine->kept("the project is open already");
		if (message->settingsChanged && m_hasGUI)
		{
			m_project->setStateOutdated();
			refreshProject(RefreshMode::ALL_FILES);
		}
	}
	else
	{
		projectMachine->requested(projectSettingsFilePath.str());
		MessageStatus("Loading Project: " + projectSettingsFilePath.str(), false, true).dispatch();

		m_project.reset();

		if (m_hasGUI)
		{
			m_mainView->clear();
		}

		loadProject(projectSettingsFilePath, message->shardConfig, message->refreshMode);
	}
}

void Application::loadProject(const FilePath& path, const ShardConfig& shardConfig, RefreshMode refreshMode)
{
	const auto load = std::make_shared<ProjectLoad>();
	load->path = path;
	load->cause = currentCause();
	load->work.emplace(load->cause);
	load->refreshMode = refreshMode;
	load->stopWithApp.emplace(m_pipelines.get_stop_token(), ProjectLoad::RequestStop {&load->stop});
	if (m_loading)
	{
		m_loading->stop.request_stop();	// replaced: its question is withdrawn
	}
	m_loading = load;

	const bool started = load->step([&]() {
		updateRecentProjects(path);

		m_storageCache->clear();
		m_storageCache->setSubject(
			std::weak_ptr<StorageAccess>());	// TODO: check if this is really required.

		load->project = std::make_shared<Project>(
			std::make_shared<ProjectSettings>(path), m_storageCache.get(), getUUID(), hasGUI());
		load->project->setShardConfig(shardConfig);
	});

	if (!started || m_appStrand == nullptr)
	{
		// In a row: the project is the app's while it loads, as it always was.
		m_project = load->project;
		if (started)
		{
			load->step([&]() { load->project->load(getDialogView(DialogView::UseCase::GENERAL)); });
		}
		endLoad(load);
		return;
	}

	if (!load->step([&]() { return load->project->beginLoad(); }))
	{
		endLoad(load);
		return;
	}

	const execution::AnyScheduler io = m_schedulers->io();
	const auto ask = [this, load](const std::optional<Project::Question>& question) noexcept {
		return askFor(*load, question);
	};
	spawnPipeline(stdexec::write_env(
		stdexec::schedule(io) | stdexec::then([load]() noexcept { return load->readSettings(); }) |
			stdexec::let_value(ask) |
			// Answered on the thread that answered it: back to the files.
			stdexec::continues_on(io) |
			stdexec::then([this, load](int option) noexcept { return load->openIndex(option, pipelinesStopping()); }) |
			stdexec::let_value(ask) |
			stdexec::continues_on(appScheduler()) | stdexec::then([this, load](int) noexcept { endLoad(load); }),
		stdexec::prop {stdexec::get_stop_token, load->stop.get_token()}));
}

QuestionSender Application::askFor(const ProjectLoad& load, const std::optional<Project::Question>& question)
{
	const CauseScope cause(load.cause);	// the question remembers it
	if (!question)
	{
		return QuestionStateMachine::instance()->asked({}, {}, QuestionPresenter());
	}
	return getDialogView(DialogView::UseCase::GENERAL)->asked(question->text, question->options);
}

std::shared_ptr<execution::Strand> Application::startQueue(TabId tab)
{
	std::shared_ptr<execution::Strand> strand =
		m_schedulers != nullptr ? execution::Strand::create(m_schedulers->io()) : nullptr;
	execution::startQueue(tab, strand);
	return strand;
}

execution::SerialQueue::Scheduler Application::appScheduler() const
{
	return execution::queueOf(TabIds::app())->scheduler();
}

bool Application::pipelinesStopping() const
{
	return m_pipelinesStopping || AppStateMachine::instance()->isClosing();
}

void Application::endLoad(const std::shared_ptr<ProjectLoad>& load)
{
	if (m_loading != load)
	{
		return;	// a load asked for since, or a close, dropped it
	}
	m_loading.reset();
	const CauseScope cause(load->cause);

	if (!load->failure.empty())
	{
		LOG_ERROR(load->failure);
		MessageStatus(load->failure, true).dispatch();
	}

	m_project = load->project;
	if (load->opened)
	{
		load->step([&]() { load->project->finishLoad(); });
		if (!load->failure.empty())
		{
			LOG_ERROR(load->failure);
			MessageStatus(load->failure, true).dispatch();
		}
	}
	updateTitle();

	// How the load ended. A project open with its index waits for TabsController,
	// which the MessageIndexingFinished its load dispatched has the overview open
	// (or not); a project without one is open as far as loading goes.
	const std::shared_ptr<ProjectStateMachine> projectMachine = ProjectStateMachine::instance();
	if (!m_project || !m_project->isOpen())
	{
		projectMachine->failed("could not load " + load->path.str());
	}
	else if (!m_project->hasIndex() || !m_hasGUI)
	{
		projectMachine->loaded();
	}

	if (load->refreshMode != RefreshMode::NONE)
	{
		refreshProject(load->refreshMode);
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
	// The app's queue: its messages, the work posted to it, the steps of the load's
	// pipeline. An indexing run's steps run on the work pool (Project::startIndexing).
	m_appStrand = startQueue(TabIds::app());
	startQueue(TabIds::background());
	if (m_schedulers != nullptr)
	{
		execution::setWorkPool(m_schedulers->compute());	// indexing runs, and the like
	}

	MessageQueue* queue = MessageQueue::getInstance().get();
	queue->addMessageFilter(std::make_shared<MessageFilterErrorCountUpdate>());
	queue->addMessageFilter(std::make_shared<MessageFilterFocusInOut>());
	queue->addMessageFilter(std::make_shared<MessageFilterSearchAutocomplete>());

	queue->setSendMessagesAsTasks(true);
	// A parallel message's listeners (an activation's graph and code) side by side
	// on the compute pool, as senders, not on a thread each.
	if (m_schedulers != nullptr)
	{
		queue->setListenerPool([pool = m_schedulers->compute()](std::function<void()> work) {
			stdexec::start_detached(stdexec::schedule(pool) | stdexec::then(std::move(work)));
		});
	}
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
	// The indexing machine follows the request from here: what the project does
	// with it moves it on, and a request nothing came of returns it to idle.
	IndexingStateMachine::instance()->requested();

	if (m_project && checkSharedMemory())
	{
		// With questions to ask, their answers decide, and the request ends there.
		if (m_appStrand != nullptr)
		{
			if (std::optional<Project::RefreshPlan> plan = m_project->planRefresh(refreshMode);
				plan && !plan->questions.empty())
			{
				askThenRefresh({m_project, std::move(*plan), 0, currentCause(), CauseWork::Held()});
				return;
			}
		}

		m_project->refresh(getDialogView(DialogView::UseCase::INDEXING), refreshMode);

		if (!m_hasGUI && !m_project->isIndexing())
		{
			MessageQuitApplication().dispatch();
		}
	}

	IndexingStateMachine::instance()->requestHandled(m_project ? "the refresh did not start" : "no project is loaded");
}

void Application::askThenRefresh(PendingRefresh refresh)
{
	const CauseScope scope(refresh.cause);
	const std::shared_ptr<DialogView> dialog = getDialogView(DialogView::UseCase::INDEXING);
	if (m_project != refresh.project)
	{
		IndexingStateMachine::instance()->requestHandled("another project was loaded meanwhile");
		return;
	}
	if (refresh.next == refresh.plan.questions.size())
	{
		refresh.project->refreshPlanned(dialog, refresh.plan);
		IndexingStateMachine::instance()->requestHandled("the refresh did not start");
		return;
	}

	const Project::Question& question = refresh.plan.questions[refresh.next];
	QuestionSender answer = dialog->asked(question.text, question.options);
	spawnPipeline(
		std::move(answer) | stdexec::continues_on(appScheduler()) |
		stdexec::then([this, refresh = std::move(refresh)](int option) mutable noexcept {
			refreshAnswered(std::move(refresh), option);
		}));
}

void Application::refreshAnswered(PendingRefresh refresh, int option)
{
	if (pipelinesStopping())
	{
		return;
	}
	if (option == 1)
	{
		// Why it did not start: the question's answer, as the mode choice's Cancel says.
		const CauseScope scope(refresh.cause);
		const Project::Question& question = refresh.plan.questions[refresh.next];
		IndexingStateMachine::instance()->requestHandled(
			std::format("canceled: the question was answered '{}'", question.options[1]));
		return;
	}
	refresh.next++;
	askThenRefresh(std::move(refresh));
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
