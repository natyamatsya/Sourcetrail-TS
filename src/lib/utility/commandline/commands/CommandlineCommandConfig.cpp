#include "CommandlineCommandConfig.h"

#include <iostream>
#include <span>

#include "ApplicationSettings.h"
#include "CommandLineParser.h"
#include "CommandlineHelper.h"
#include "FilePath.h"
#include "GlazeCli.h"
#include "logging.h"

namespace glz
{
template <>
struct meta<commandline::ConfigOpts>
{
	using T = commandline::ConfigOpts;
	static constexpr auto value = object("indexer-threads", &T::indexer_threads, "logging-enabled",
		&T::logging_enabled, "verbose-indexer-logging-enabled", &T::verbose_indexer_logging_enabled,
		"global-header-search-paths", &T::global_header_search_paths, "global-framework-search-paths",
		&T::global_framework_search_paths, "show", &T::show);
};
}	 // namespace glz

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

void CommandlineCommandConfig::setup() {}

void CommandlineCommandConfig::printHelp()
{
	std::cout << "Usage: Sourcetrail config [options]\n\nOptions:\n"
			  << glzcli::help<ConfigOpts>() << std::endl;
}

CommandlineCommand::ReturnStatus CommandlineCommandConfig::parse(std::vector<std::string>& args)
{
	if (args.empty())
	{
		printHelp();
		return ReturnStatus::CMD_QUIT;
	}

	if (const auto stop = earlyExit(glzcli::parse(m_opts, std::span<const std::string>(args))))
		return *stop;

	ApplicationSettings* settings = ApplicationSettings::getInstance().get();
	if (settings == nullptr)
	{
		LOG_ERROR("No application settings loaded");
		return ReturnStatus::CMD_QUIT;
	}

	if (m_opts.show)
	{
		std::cout << "Sourcetrail Settings:\n"
				  << "\n  indexer-threads: " << settings->getIndexerThreadCount()
				  << "\n  logging-enabled: " << settings->getLoggingEnabled()
				  << "\n  verbose-indexer-logging-enabled: " << settings->getVerboseIndexerLoggingEnabled();
		printVector("global-header-search-paths", settings->getHeaderSearchPaths());
		printVector("global-framework-search-paths", settings->getFrameworkSearchPaths());
		return ReturnStatus::CMD_QUIT;
	}

	if (m_opts.indexer_threads.has_value())
	{
		settings->setIndexerThreadCount(*m_opts.indexer_threads);
	}
	if (m_opts.logging_enabled.has_value())
	{
		settings->setLoggingEnabled(*m_opts.logging_enabled);
	}
	if (m_opts.verbose_indexer_logging_enabled.has_value())
	{
		settings->setVerboseIndexerLoggingEnabled(*m_opts.verbose_indexer_logging_enabled);
	}
	if (!m_opts.global_header_search_paths.empty())
	{
		settings->setHeaderSearchPaths(extractPaths(m_opts.global_header_search_paths));
	}
	if (!m_opts.global_framework_search_paths.empty())
	{
		settings->setFrameworkSearchPaths(extractPaths(m_opts.global_framework_search_paths));
	}

	settings->save();
	return ReturnStatus::CMD_QUIT;
}

}	 // namespace commandline
