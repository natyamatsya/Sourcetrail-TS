#include "CommandlineCommandIndex.h"

#include <iostream>
#include <span>

#include "CommandLineParser.h"
#ifndef SRCTRL_MODULE_BUILD
#include "FilePath.h"
#endif
#include "GlazeCli.h"
#include "ShardConfig.h"

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.file;
#endif

namespace glz
{
template <>
struct meta<commandline::IndexOpts>
{
	using T = commandline::IndexOpts;
	static constexpr auto value = object("incomplete", &T::incomplete, "full", &T::full, "shard",
		&T::shard, "shard-output", &T::shard_output, "project-file", &T::project_file);
};
}	 // namespace glz

namespace commandline
{
CommandlineCommandIndex::CommandlineCommandIndex(CommandLineParser* parser)
	: CommandlineCommand("index", "Index a certain project.", parser)
{
}

CommandlineCommandIndex::~CommandlineCommandIndex() = default;

void CommandlineCommandIndex::setup() {}

void CommandlineCommandIndex::printHelp()
{
	std::cout << "Usage: Sourcetrail index [options] [project-file]\n\nOptions:\n"
			  << glzcli::help<IndexOpts>() << std::endl;
}

CommandlineCommand::ReturnStatus CommandlineCommandIndex::parse(std::vector<std::string>& args)
{
	if (args.empty())
	{
		printHelp();
		return ReturnStatus::CMD_QUIT;
	}

	if (const auto stop = earlyExit(glzcli::parse(m_opts, std::span<const std::string>(args))))
		return *stop;

	if (m_opts.full)
		m_parser->fullRefresh();
	else if (m_opts.incomplete)
		m_parser->incompleteRefresh();

	if (!m_opts.project_file.empty())
		m_parser->setProjectFile(FilePath(m_opts.project_file));

	if (!m_opts.shard.empty())
	{
		std::size_t index = 0;
		std::size_t count = 0;
		const std::size_t slash = m_opts.shard.find('/');
		try
		{
			if (slash != std::string::npos)
			{
				index = std::stoul(m_opts.shard.substr(0, slash));
				count = std::stoul(m_opts.shard.substr(slash + 1));
			}
		}
		catch (const std::exception&)
		{
			// fall through to the validation error below
		}

		if (index < 1 || count < 2 || index > count)
		{
			std::cerr << "ERROR: --shard expects i/N with 1 <= i <= N and N >= 2, got \""
					  << m_opts.shard << "\"" << std::endl;
			return ReturnStatus::CMD_FAILURE;
		}
		if (!m_opts.full)
		{
			std::cerr << "ERROR: --shard requires --full (incremental shard runs are not supported)"
					  << std::endl;
			return ReturnStatus::CMD_FAILURE;
		}

		ShardConfig config;
		config.index = index;
		config.count = count;
		if (!m_opts.shard_output.empty())
		{
			config.outputPath = FilePath(m_opts.shard_output);
		}
		m_parser->setShardConfig(config);
	}
	else if (!m_opts.shard_output.empty())
	{
		std::cerr << "ERROR: --shard-output requires --shard" << std::endl;
		return ReturnStatus::CMD_FAILURE;
	}

	return ReturnStatus::CMD_OK;
}

}	 // namespace commandline
