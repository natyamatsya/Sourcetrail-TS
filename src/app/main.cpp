#include "setupApp.h"

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

	Version version = setupAppDirectories(FilePath(argv[0]).getCanonical().getParentDirectory());

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
		return 0;
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
			return 0;
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

		// Resolve the color scheme through the system Day/Night appearance (when the
		// user opted in). Installed before createInstance() so the very first style
		// load already reflects the OS appearance; the watcher reacts to live changes.
		Application::setColorSchemePathProvider(&QtColorSchemeWatcher::resolveColorSchemePath);
		QtColorSchemeWatcher colorSchemeWatcher;

		QtViewFactory viewFactory;
		QtNetworkFactory networkFactory;

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
		if (!applicationSettings->getLoggingEnabled())
			closeConsoleWindow();

		addLanguagePackages();

		utility::loadFontsFromDirectory(ResourcePaths::getFontsDirectoryPath(), ".otf");
		utility::loadFontsFromDirectory(ResourcePaths::getFontsDirectoryPath(), ".ttf");

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

		return QtApplication::exec();
	}
}
