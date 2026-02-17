#include "CommandlineCommandConfig.h"

#include <iostream>
#include <optional>

#include "ApplicationSettings.h"
#include "CommandLineParser.h"
#include "CommandlineHelper.h"
#include "FilePath.h"
#include "logging.h"

namespace commandline
{
namespace
{
void printVector(const std::string& title, const std::vector<FilePath>& vec)
{
	std::cout << "\n  " << title << ":";
	if (vec.empty())
	{
		std::cout << "\n    -\n";
	}
	for (const FilePath& item: vec)
	{
		std::cout << "\n    " << item.str();
	}
}
}	 // namespace

CommandlineCommandConfig::CommandlineCommandConfig(CommandLineParser* parser)
	: CommandlineCommand("config", "Change preferences relevant to project indexing.", parser)
{
}

CommandlineCommandConfig::~CommandlineCommandConfig() = default;

void CommandlineCommandConfig::setup()
{
	m_app.add_option("-t,--indexer-threads", m_indexerThreads,
		"Set the number of threads used for indexing (0 uses ideal thread count)");
	m_app.add_option("-p,--use-processes", m_useProcesses,
		"Enable C/C++ Indexer threads to run in different processes. <true/false>")->expected(1);
	m_app.add_option("-l,--logging-enabled", m_loggingEnabled,
		"Enable file/console logging <true/false>")->expected(1);
	m_app.add_option("-L,--verbose-indexer-logging-enabled", m_verboseIndexerLogging,
		"Enable additional log of abstract syntax tree during the indexing. <true/false> WARNING Slows down indexing speed")->expected(1);
	m_app.add_option("-j,--jvm-path", m_jvmPath,
		"Path to the location of the jvm library");
	m_app.add_option("-m,--maven-path", m_mavenPath,
		"Path to the maven binary");
	m_app.add_option("-J,--jre-system-library-paths", m_jreSystemLibraryPaths,
		"paths to the jars of the JRE system library. "
		"These jars can be found inside your JRE install directory (once per path or comma separated)");
	m_app.add_option("-g,--global-header-search-paths", m_globalHeaderSearchPaths,
		"Global include paths (once per path or comma separated)");
	m_app.add_option("-F,--global-framework-search-paths", m_globalFrameworkSearchPaths,
		"Global include paths (once per path or comma separated)");
	m_app.add_flag("-s,--show", m_show, "displays all settings");
}

CommandlineCommand::ReturnStatus CommandlineCommandConfig::parse(std::vector<std::string>& args)
{
	// Check before parsing — CLI11 modifies args
	const bool showHelp = args.empty() || args[0] == "help";

	if (showHelp)
	{
		printHelp();
		return ReturnStatus::CMD_QUIT;
	}

	try
	{
		std::vector<std::string> fullArgs{m_name};
		fullArgs.insert(fullArgs.end(), args.begin(), args.end());
		std::vector<const char*> argv;
		for (const auto& a : fullArgs)
			argv.push_back(a.c_str());
		m_app.parse(static_cast<int>(argv.size()), argv.data());
	}
	catch (const CLI::ParseError& e)
	{
		if (e.get_exit_code() == 0)
		{
			printHelp();
			return ReturnStatus::CMD_QUIT;
		}
		std::cerr << "ERROR: " << e.what() << std::endl;
		return ReturnStatus::CMD_FAILURE;
	}

	ApplicationSettings* settings = ApplicationSettings::getInstance().get();
	if (settings == nullptr)
	{
		LOG_ERROR("No application settings loaded");
		return ReturnStatus::CMD_QUIT;
	}

	if (m_show)
	{
		std::cout << "Sourcetrail Settings:\n"
				  << "\n  indexer-threads: " << settings->getIndexerThreadCount()
				  << "\n  use-processes: " << settings->getMultiProcessIndexingEnabled()
				  << "\n  logging-enabled: " << settings->getLoggingEnabled()
				  << "\n  verbose-indexer-logging-enabled: "
				  << settings->getVerboseIndexerLoggingEnabled()
				  << "\n  jvm-path: " << settings->getJavaPath().str()
				  << "\n  maven-path: " << settings->getMavenPath().str();
		printVector("global-header-search-paths", settings->getHeaderSearchPaths());
		printVector("global-framework-search-paths", settings->getFrameworkSearchPaths());
		printVector("jre-system-library-paths", settings->getJreSystemLibraryPaths());
		return ReturnStatus::CMD_QUIT;
	}

	if (m_indexerThreads.has_value())
		settings->setIndexerThreadCount(*m_indexerThreads);
	if (m_useProcesses.has_value())
		settings->setMultiProcessIndexingEnabled(*m_useProcesses);
	if (m_loggingEnabled.has_value())
		settings->setLoggingEnabled(*m_loggingEnabled);
	if (m_verboseIndexerLogging.has_value())
		settings->setVerboseIndexerLoggingEnabled(*m_verboseIndexerLogging);

	if (!m_jvmPath.empty())
	{
		FilePath path(m_jvmPath);
		if (!path.exists())
			std::cout << "\nWARNING: " << path.str() << " does not exist." << std::endl;
		settings->setJavaPath(path);
	}
	if (!m_mavenPath.empty())
	{
		FilePath path(m_mavenPath);
		if (!path.exists())
			std::cout << "\nWARNING: " << path.str() << " does not exist." << std::endl;
		settings->setMavenPath(path);
	}

	if (!m_jreSystemLibraryPaths.empty())
		settings->setJreSystemLibraryPaths(extractPaths(m_jreSystemLibraryPaths));
	if (!m_globalHeaderSearchPaths.empty())
		settings->setHeaderSearchPaths(extractPaths(m_globalHeaderSearchPaths));
	if (!m_globalFrameworkSearchPaths.empty())
		settings->setFrameworkSearchPaths(extractPaths(m_globalFrameworkSearchPaths));

	settings->save();

	return ReturnStatus::CMD_QUIT;
}

}	 // namespace commandline
