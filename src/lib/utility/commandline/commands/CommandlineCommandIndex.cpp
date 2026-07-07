#include "CommandlineCommandIndex.h"

#include <iostream>

#include "CommandLineParser.h"
#include "ShardConfig.h"

namespace commandline
{
CommandlineCommandIndex::CommandlineCommandIndex(CommandLineParser* parser)
	: CommandlineCommand("index", "Index a certain project.", parser)
{
}

CommandlineCommandIndex::~CommandlineCommandIndex() = default;

void CommandlineCommandIndex::setup()
{
	m_app.add_flag("-i,--incomplete", "Also reindex incomplete files (files with errors)");
	m_app.add_flag("-f,--full", "Index full project (omit to only index new/changed files)");
	m_app.add_option(
		"--shard",
		m_shardArg,
		"Distributed indexing: index only the i-th of N deterministic stripes of the "
		"project's translation units into a standalone shard DB (format: i/N, 1-based; "
		"requires --full). Combine shard DBs with the 'merge' command.");
	m_app.add_option(
		"--shard-output",
		m_shardOutputArg,
		"Output path for the shard DB (default: <project>.shard<i>of<N>.srctrl.db)");
	m_app.add_option(
		"project-file",
		m_projectFileArg,
		"Project file to index (.srctrl.toml)");
}

CommandlineCommand::ReturnStatus CommandlineCommandIndex::parse(std::vector<std::string>& args)
{
	if (args.empty() || args[0] == "help")
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

	if (m_app.count("--full"))
		m_parser->fullRefresh();
	else if (m_app.count("--incomplete"))
		m_parser->incompleteRefresh();

	if (!m_projectFileArg.empty())
		m_parser->setProjectFile(FilePath(m_projectFileArg));

	if (m_app.count("--shard"))
	{
		size_t index = 0;
		size_t count = 0;
		const size_t slash = m_shardArg.find('/');
		try
		{
			if (slash != std::string::npos)
			{
				index = std::stoul(m_shardArg.substr(0, slash));
				count = std::stoul(m_shardArg.substr(slash + 1));
			}
		}
		catch (const std::exception&)
		{
			// fall through to the validation error below
		}

		if (index < 1 || count < 2 || index > count)
		{
			std::cerr << "ERROR: --shard expects i/N with 1 <= i <= N and N >= 2, got \""
					  << m_shardArg << "\"" << std::endl;
			return ReturnStatus::CMD_FAILURE;
		}
		if (!m_app.count("--full"))
		{
			std::cerr << "ERROR: --shard requires --full (incremental shard runs are not "
						 "supported)"
					  << std::endl;
			return ReturnStatus::CMD_FAILURE;
		}

		ShardConfig config;
		config.index = index;
		config.count = count;
		if (!m_shardOutputArg.empty())
		{
			config.outputPath = FilePath(m_shardOutputArg);
		}
		m_parser->setShardConfig(config);
	}
	else if (m_app.count("--shard-output"))
	{
		std::cerr << "ERROR: --shard-output requires --shard" << std::endl;
		return ReturnStatus::CMD_FAILURE;
	}

	return ReturnStatus::CMD_OK;
}

}	 // namespace commandline
