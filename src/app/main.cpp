#include "setupApp.h"

#include "AppStateMachine.h"
#include "Application.h"
#include "ApplicationSettings.h"
#include "ApplicationSettingsPrefiller.h"
#include "CommandLineParser.h"
#include "ConsoleLogger.h"
#include "FileLogger.h"
#include "LanguagePackageManager.h"
#include "LogManager.h"
#include "MessageIndexingInterrupted.h"
#include "MessageLoadProject.h"
#include "MessageStatus.h"
#include "Platform.h"
#include "QtApplication.h"
#include "QtColorSchemeWatcher.h"
#include "QtCoreApplication.h"
#include "QtNetworkFactory.h"
#include "QtScreenshot.h"
#include "QtUiSnapshot.h"
#include "QtViewFactory.h"
#include "ResourcePaths.h"
#include "Schedulers.h"
#include "ScopedFunctor.h"
#include "SourceGroupFactory.h"
#include "SourceGroupFactoryModuleCustom.h"
#include "language_package_flags.h"
#include "utilityQt.h"

#if BUILD_CXX_LANGUAGE_PACKAGE
	#include "CxxIndexerCommandCodec.h"
	#include "LanguagePackageCxx.h"
	#include "SourceGroupFactoryModuleCxx.h"
#endif

#include "LanguagePackageRust.h"
#include "SourceGroupFactoryModuleRust.h"

#include "LanguagePackageSwift.h"
#include "SourceGroupFactoryModuleSwift.h"

#include "LanguagePackageZig.h"
#include "SourceGroupFactoryModuleZig.h"


#include <QByteArray>
#include <QCoreApplication>
#include <QMetaObject>
#include <QtEnvironmentVariables>

#include <csignal>
#include <iostream>

void closeConsoleWindow()
{
#if BOOST_OS_WINDOWS
	// Hide the console which Windows creates if Sourcetrail was not started from one:
	if (HWND consoleWnd = GetConsoleWindow(); consoleWnd != nullptr) {
		DWORD consoleOwnerProcessId;
		if (GetWindowThreadProcessId(consoleWnd, &consoleOwnerProcessId) != 0) {
			if (consoleOwnerProcessId == GetCurrentProcessId()) {
				// Hiding will not work if the default terminal is *not* the 'Windows console host'
				// as is the case for Windows 11. See https://github.com/petermost/Sourcetrail/issues/19
				// for further details.

				ShowWindow(consoleWnd, SW_HIDE);
			}
		}
	}
#endif
}

// --user-data-dir (also SOURCETRAIL_USER_DATA_DIR): where settings, logs and the
// default projects live, empty = the per-user location. Read from argv directly
// because the user directory is set up before the command line is parsed; the
// parser declares the option too, so it accepts it.
FilePath userDataDirFromCommandLine(int argc, char* argv[])
{
	const std::string option = "--user-data-dir";
	for (int i = 1; i < argc; i++)
	{
		const std::string arg = argv[i];
		if (arg == option && i + 1 < argc)
		{
			return FilePath(argv[i + 1]);
		}
		if (arg.starts_with(option + "="))
		{
			return FilePath(arg.substr(option.size() + 1));
		}
	}
	const QByteArray fromEnvironment = qgetenv("SOURCETRAIL_USER_DATA_DIR");
	return FilePath(fromEnvironment.toStdString());
}

void signalHandler(int  /*signum*/)
{
	std::cout << "interrupt indexing" << std::endl;
	MessageIndexingInterrupted().dispatch();
}

void setupLogging()
{
	LogManager* logManager = LogManager::getInstance().get();

	std::shared_ptr<ConsoleLogger> consoleLogger = std::make_shared<ConsoleLogger>();
	consoleLogger->setLogLevel(Logger::LOG_ALL);
	logManager->addLogger(consoleLogger);

	std::shared_ptr<FileLogger> fileLogger = std::make_shared<FileLogger>();
	fileLogger->setLogLevel(Logger::LOG_ALL);
	fileLogger->deleteLogFiles(FileLogger::generateDatedFileName("log", "", -30));
	logManager->addLogger(fileLogger);
}

template <bool Enabled, typename Module, typename Package>
void addLanguagePackage()
{
	if constexpr (Enabled)
	{
		SourceGroupFactory::getInstance()->addModule(std::make_shared<Module>());
		LanguagePackageManager::getInstance()->addPackage(std::make_shared<Package>());
	}
}

void addLanguagePackages()
{
	SourceGroupFactory::getInstance()->addModule(std::make_shared<SourceGroupFactoryModuleCustom>());

#if BUILD_CXX_LANGUAGE_PACKAGE
	addLanguagePackage<language_packages::buildCxxLanguagePackage,   SourceGroupFactoryModuleCxx,   LanguagePackageCxx>();
	registerCxxIndexerCommandCodec();
#endif
	addLanguagePackage<language_packages::buildRustLanguagePackage,  SourceGroupFactoryModuleRust,  LanguagePackageRust>();
	addLanguagePackage<language_packages::buildSwiftLanguagePackage, SourceGroupFactoryModuleSwift, LanguagePackageSwift>();
	addLanguagePackage<language_packages::buildZigLanguagePackage,   SourceGroupFactoryModuleZig,   LanguagePackageZig>();
}

int main(int argc, char* argv[])
{
	setupDefaultLocale();

	Version version = setupAppDirectories(
		FilePath(argv[0]).getCanonical().getParentDirectory(), userDataDirFromCommandLine(argc, argv));

	if constexpr (utility::Platform::isLinux())
	{
		if (qgetenv("SOURCETRAIL_VIA_SCRIPT").isNull())
		{
			std::cout << "ERROR: Please run Sourcetrail via the Sourcetrail.sh script!" << std::endl;
		}
	}
	MessageStatus(std::string("Starting Sourcetrail version ") + version.toDisplayString())
		.dispatch();

	commandline::CommandLineParser commandLineParser(version.toDisplayString());
	commandLineParser.preparse(argc, argv);
	if (commandLineParser.exitApplication())
	{
		return commandLineParser.exitCode();
	}

	setupAppEnvironment(argc, argv);

	if (commandLineParser.runWithoutGUI())
	{
		// headless Sourcetrail
		[[maybe_unused]]
		QtCoreApplication qtApp(argc, argv);

		setupLogging();

		Application::createInstance(version, nullptr, nullptr);
		
		[[maybe_unused]]
		ScopedFunctor f([]()
		{
			Application::destroyInstance();
		});

		ApplicationSettingsPrefiller::prefillPaths(ApplicationSettings::getInstance().get());
		addLanguagePackages();

		signal(SIGINT, signalHandler);
		signal(SIGTERM, signalHandler);
		signal(SIGABRT, signalHandler);

		commandLineParser.parse();

		if (commandLineParser.exitApplication())
		{
			return commandLineParser.exitCode();
		}

		if (commandLineParser.hasError())
		{
			// Nothing was dispatched, so nothing will ever quit the event loop —
			// entering exec() here used to hang the headless run forever.
			std::cout << commandLineParser.getError() << std::endl;
			return 1;
		}

		MessageLoadProject(
			commandLineParser.getProjectFilePath(),
			false,
			commandLineParser.getRefreshMode(),
			commandLineParser.getShardConfig())
			.dispatch();

		return QtCoreApplication::exec();
	}
	else
	{
		// Headless screenshot mode: render the real GUI with no display. Must be set
		// before the QApplication is constructed. Respect an explicit override.
		if ((!commandLineParser.getScreenshotPath().empty() ||
			 !commandLineParser.getUiSnapshotPath().empty()) &&
			qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
		{
			qputenv("QT_QPA_PLATFORM", "offscreen");
		}

		[[maybe_unused]]
		QtApplication qtApp(argc, argv);

		setupLogging();

		// The bundled fonts before the first style load (createInstance), which names them: a
		// family the style asks for before it is there makes Qt search for a substitute. They
		// are logged once logging is on, after createInstance.
		std::set<std::string> fontFamilies{
			utility::loadFontsFromDirectory(ResourcePaths::getFontsDirectoryPath(), ".otf")};
		fontFamilies.merge(
			utility::loadFontsFromDirectory(ResourcePaths::getFontsDirectoryPath(), ".ttf"));

		// Resolve the color scheme through the system Day/Night appearance (when the
		// user opted in). Installed before createInstance() so the very first style
		// load already reflects the OS appearance; the watcher reacts to live changes.
		Application::setColorSchemePathProvider(&QtColorSchemeWatcher::resolveColorSchemePath);
		QtColorSchemeWatcher colorSchemeWatcher;

		QtViewFactory viewFactory;
		QtNetworkFactory networkFactory;

		// An agent's quit ends the app as closing its window does: the event loop
		// ends, modal loops with it, and the teardown runs. Before createInstance,
		// which opens agent control.
		AppStateMachine::instance()->onQuit([]() {
			QMetaObject::invokeMethod(qApp, []() { QCoreApplication::exit(0); }, Qt::QueuedConnection);
		});

		Application::createInstance(
			version,
			&viewFactory,
			&networkFactory,
			&execution::qt::Schedulers::getInstance(),
			/*enableAgentControl*/ true,	// always on in agent builds; namespace via --agent-instance
			commandLineParser.getAgentInstanceId());
		
		[[maybe_unused]]
		ScopedFunctor f([]()
		{
			Application::destroyInstance();
		});

		auto applicationSettings = ApplicationSettings::getInstance();
		ApplicationSettingsPrefiller::prefillPaths(applicationSettings.get());
		if (commandLineParser.getNoAnimations())
			ApplicationSettings::suppressAnimations();
		if (!applicationSettings->getLoggingEnabled())
			closeConsoleWindow();

		for (const std::string& family: fontFamilies)
		{
			LOG_INFO("Loaded FontFamily: " + family);
		}

		addLanguagePackages();

		if (commandLineParser.hasError())
		{
			Application::getInstance()->handleDialog(commandLineParser.getError());
		}
		else
		{
			MessageLoadProject(commandLineParser.getProjectFilePath(), false, RefreshMode::NONE).dispatch();
		}

		if (!commandLineParser.getScreenshotPath().empty())
		{
			utility::qt::scheduleScreenshotAndQuit(
				commandLineParser.getScreenshotPath(), commandLineParser.getScreenshotDelayMs());
		}

		if (!commandLineParser.getUiSnapshotPath().empty())
		{
			const auto format = (commandLineParser.getUiSnapshotFormat() == "object")
				? utility::qt::SnapshotFormat::ObjectTree
				: utility::qt::SnapshotFormat::Accessibility;
			utility::qt::scheduleSnapshotAndQuit(
				commandLineParser.getUiSnapshotPath(), format, commandLineParser.getScreenshotDelayMs());
		}

		// Started: the language packages registered, the settings prefilled, the
		// startup load queued. What waited for it (a load an agent asked for while
		// the app started) goes now, behind the startup load.
		AppStateMachine::instance()->started();

		return QtApplication::exec();
	}
}
