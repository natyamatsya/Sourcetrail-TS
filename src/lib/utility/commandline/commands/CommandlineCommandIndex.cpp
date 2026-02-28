#include "CommandlineCommandIndex.h"

#include <iostream>

#include "CommandLineParser.h"

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

	return ReturnStatus::CMD_OK;
}

}	 // namespace commandline
