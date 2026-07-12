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
#include "QtCoreApplication.h"
#include "QtNetworkFactory.h"
#include "QtScreenshot.h"
#include "QtViewFactory.h"
#include "ResourcePaths.h"
#include "Schedulers.h"
#include "ScopedFunctor.h"
#include "SourceGroupFactory.h"
#include "SourceGroupFactoryModuleCustom.h"
#include "language_package_flags.h"
#include "utilityQt.h"

#if BUILD_CXX_LANGUAGE_PACKAGE
	#include "LanguagePackageCxx.h"
	#include "SourceGroupFactoryModuleCxx.h"
#endif

#include "LanguagePackageRust.h"
#include "SourceGroupFactoryModuleRust.h"

#include "LanguagePackageSwift.h"
#include "SourceGroupFactoryModuleSwift.h"


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
#endif
	addLanguagePackage<language_packages::buildRustLanguagePackage,  SourceGroupFactoryModuleRust,  LanguagePackageRust>();
	addLanguagePackage<language_packages::buildSwiftLanguagePackage, SourceGroupFactoryModuleSwift, LanguagePackageSwift>();
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
			std::cout << commandLineParser.getError() << std::endl;
		}
		else
		{
			MessageLoadProject(
				commandLineParser.getProjectFilePath(),
				false,
				commandLineParser.getRefreshMode(),
				commandLineParser.getShardConfig())
				.dispatch();
		}

		return QtCoreApplication::exec();
	}
	else
	{
		// Headless screenshot mode: render the real GUI with no display. Must be set
		// before the QApplication is constructed. Respect an explicit override.
		if (!commandLineParser.getScreenshotPath().empty() &&
			qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
		{
			qputenv("QT_QPA_PLATFORM", "offscreen");
		}

		[[maybe_unused]]
		QtApplication qtApp(argc, argv);

		setupLogging();

		QtViewFactory viewFactory;
		QtNetworkFactory networkFactory;

		Application::createInstance(
			version,
			&viewFactory,
			&networkFactory,
			&execution::qt::Schedulers::getInstance(),
			commandLineParser.getAgentControlEnabled(),
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

		return QtApplication::exec();
	}
}
