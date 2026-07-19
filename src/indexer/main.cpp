
#include <array>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "language_package_flags.h"
#include "AppPath.h"
#include "ApplicationSettings.h"
#include "CxxModulePrebuildRunner.h"
#include "CxxPchBuildRunner.h"
#include "FileLogger.h"
#include "GlazeCli.h"
#include "InterprocessIndexer.h"
#include "LanguagePackageManager.h"
#include "LogManager.h"
#include "UserPaths.h"
#include "logging.h"
#include "setupApp.h"

#if BUILD_CXX_LANGUAGE_PACKAGE
	#include "CxxIndexerCommandCodec.h"
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

// The indexer's argv is an app->worker contract, not a user CLI: the app launches it with five
// positionals (see TaskBuildIndex / runIndexerPrebuildMode) plus at most one of the "--key=value"
// flags. Described declaratively for the shared glaze reflection parser (glzcli), mirroring the app's
// own command structs -- glzcli splits flags from positionals (so an optional flag never shifts the
// positional layout) and leaves absent trailing positionals at their defaults.
struct IndexerOptions
{
	std::string process_id;		 // a small integer; parsed to a ProcessId below
	std::string instance_uuid;	 // the shared-memory IPC channel id (unused in prebuild modes)
	std::string app_path;		 // shared data directory
	std::string user_data_path;	 // user data directory
	std::string log_file_path;	 // optional; logging is off when empty
	std::string only_group_id;	 // fan-out: pin this worker to one source group
	std::string prebuild_modules;	 // C++20 module-prebuild request path (mode switch)
	std::string prebuild_pch;		 // PCH-prebuild request path (mode switch)

	static constexpr std::array<std::string_view, 5> positionals = {
		"process-id", "instance-uuid", "app-path", "user-data-path", "log-file-path"};
};

namespace glz
{
template <>
struct meta<IndexerOptions>
{
	using T = IndexerOptions;
	static constexpr auto value = object(
		"process-id", &T::process_id,
		"instance-uuid", &T::instance_uuid,
		"app-path", &T::app_path,
		"user-data-path", &T::user_data_path,
		"log-file-path", &T::log_file_path,
		"only-group-id", &T::only_group_id,
		"prebuild-modules", &T::prebuild_modules,
		"prebuild-pch", &T::prebuild_pch);
};
}	 // namespace glz

int main(int argc, char* argv[])
{
	setupDefaultLocale();

	IndexerOptions options;
	const std::vector<std::string> args(argv + 1, argv + argc);
	if (const glzcli::ParseResult parseResult =
			glzcli::parse(options, std::span<const std::string>(args));
		parseResult.error)
	{
		// Logging isn't configured yet (the log path is one of these very arguments).
		std::fprintf(stderr, "sourcetrail_indexer: %s\n", parseResult.error->c_str());
		return 1;
	}

	const ProcessId processId = options.process_id.empty()
		? ProcessId::INVALID
		: ProcessId(std::stoi(options.process_id));
	const std::string& instanceUuid = options.instance_uuid;
	const std::string& onlyGroupId = options.only_group_id;
	const std::string& prebuildModulesRequest = options.prebuild_modules;
	const std::string& prebuildPchRequest = options.prebuild_pch;

	AppPath::setSharedDataDirectoryPath(FilePath(options.app_path));
	UserPaths::setUserDataDirectoryPath(FilePath(options.user_data_path));

	if (!options.log_file_path.empty())
	{
		setupLogging(FilePath(options.log_file_path));
	}

	suppressCrashMessage();

	ApplicationSettings* appSettings = ApplicationSettings::getInstance().get();
	appSettings->load(UserPaths::getAppSettingsFilePath());
	LogManager::getInstance()->setLoggingEnabled(appSettings->getLoggingEnabled());

	LOG_INFO("sharedDataPath: " + AppPath::getSharedDataDirectoryPath().str());
	LOG_INFO("userDataPath: " + UserPaths::getUserDataDirectoryPath().str());


#if BUILD_CXX_LANGUAGE_PACKAGE
	LanguagePackageManager::getInstance()->addPackage(std::make_shared<LanguagePackageCxx>());
	registerCxxIndexerCommandCodec();
#endif

	// Module-prebuild mode: scan + build C++20 BMIs for a source group, then exit -- a separate,
	// crash-isolated invocation rather than the shared-memory indexing loop below.
	if (!prebuildModulesRequest.empty())
	{
		return CxxModulePrebuildRunner::run(FilePath(prebuildModulesRequest));
	}
	if (!prebuildPchRequest.empty())
	{
		return CxxPchBuildRunner::run(FilePath(prebuildPchRequest));
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
