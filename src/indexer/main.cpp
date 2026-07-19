
#include <string>
#include <vector>

#include "language_package_flags.h"
#include "AppPath.h"
#include "ApplicationSettings.h"
#include "CxxModulePrebuildRunner.h"
#include "FileLogger.h"
#include "InterprocessIndexer.h"
#include "LanguagePackageManager.h"
#include "LogManager.h"
#include "UserPaths.h"
#include "logging.h"
#include "setupApp.h"

#if BUILD_CXX_LANGUAGE_PACKAGE
	#include "LanguagePackageCxx.h"
#endif

#if BOOST_OS_WINDOWS
	#include <Windows.h>
#endif

void setupLogging(const FilePath& logFilePath)
{
	LogManager* logManager = LogManager::getInstance().get();

	// std::shared_ptr<ConsoleLogger> consoleLogger = std::make_shared<ConsoleLogger>();
	// // consoleLogger->setLogLevel(Logger::LOG_WARNINGS | Logger::LOG_ERRORS);
	// consoleLogger->setLogLevel(Logger::LOG_ALL);
	// logManager->addLogger(consoleLogger);

	std::shared_ptr<FileLogger> fileLogger = std::make_shared<FileLogger>();
	fileLogger->setLogFilePath(logFilePath);
	fileLogger->setLogLevel(Logger::LOG_ALL);
	logManager->addLogger(fileLogger);
}

void suppressCrashMessage()
{
#if BOOST_OS_WINDOWS
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
}

int main(int argc, char* argv[])
{
	setupDefaultLocale();
	
	ProcessId processId = ProcessId::INVALID;
	std::string instanceUuid;
	std::string appPath;
	std::string userDataPath;
	std::string logFilePath;
	std::string onlyGroupId;
	std::string prebuildModulesRequest;

	// Split "--key=value" flags from the positional arguments, so optional flags
	// (like the fan-out group pin) don't shift the positional layout.
	std::vector<std::string> positional;
	for (int i = 1; i < argc; i++)
	{
		const std::string arg = argv[i];
		if (const std::string groupPrefix = "--only-group-id="; arg.starts_with(groupPrefix))
		{
			onlyGroupId = arg.substr(groupPrefix.size());
		}
		else if (const std::string prebuildPrefix = "--prebuild-modules=";
				 arg.starts_with(prebuildPrefix))
		{
			prebuildModulesRequest = arg.substr(prebuildPrefix.size());
		}
		else
		{
			positional.push_back(arg);
		}
	}

	if (positional.size() >= 1)
	{
		processId = ProcessId(std::stoi(positional[0]));
	}

	if (positional.size() >= 2)
	{
		instanceUuid = positional[1];
	}

	if (positional.size() >= 3)
	{
		appPath = positional[2];
	}

	if (positional.size() >= 4)
	{
		userDataPath = positional[3];
	}

	if (positional.size() >= 5)
	{
		logFilePath = positional[4];
	}

	AppPath::setSharedDataDirectoryPath(FilePath(appPath));
	UserPaths::setUserDataDirectoryPath(FilePath(userDataPath));

	if (!logFilePath.empty())
	{
		setupLogging(FilePath(logFilePath));
	}

	suppressCrashMessage();

	ApplicationSettings* appSettings = ApplicationSettings::getInstance().get();
	appSettings->load(UserPaths::getAppSettingsFilePath());
	LogManager::getInstance()->setLoggingEnabled(appSettings->getLoggingEnabled());

	LOG_INFO("sharedDataPath: " + AppPath::getSharedDataDirectoryPath().str());
	LOG_INFO("userDataPath: " + UserPaths::getUserDataDirectoryPath().str());


#if BUILD_CXX_LANGUAGE_PACKAGE
	LanguagePackageManager::getInstance()->addPackage(std::make_shared<LanguagePackageCxx>());
#endif

	// Module-prebuild mode: scan + build C++20 BMIs for a source group, then exit -- a separate,
	// crash-isolated invocation rather than the shared-memory indexing loop below.
	if (!prebuildModulesRequest.empty())
	{
		return CxxModulePrebuildRunner::run(FilePath(prebuildModulesRequest));
	}

	InterprocessIndexer indexer(instanceUuid, processId, onlyGroupId);
	const InterprocessIndexer::WorkResult workResult = indexer.work();
	if (!workResult)
	{
		LOG_ERROR_STREAM(<< "Indexer worker failed: " << workResult.error());
		return 1;
	}

	return 0;
}
