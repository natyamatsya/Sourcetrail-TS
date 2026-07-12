#include "CommandlineCommand.h"

#include <iostream>

#include "GlazeCli.h"

namespace commandline
{
std::optional<CommandlineCommand::ReturnStatus> CommandlineCommand::earlyExit(
	const glzcli::ParseResult& result)
{
	if (result.help)
	{
		printHelp();
		return ReturnStatus::CMD_QUIT;
	}
	if (result.error)
	{
		std::cerr << "ERROR: " << *result.error << std::endl;
		return ReturnStatus::CMD_FAILURE;
	}
	return std::nullopt;
}

CommandlineCommand::CommandlineCommand(
	const std::string& name, const std::string& description, CommandLineParser* parser)
	: m_name(name), m_description(description), m_parser(parser)
{
}

CommandlineCommand::~CommandlineCommand() = default;

const std::string& CommandlineCommand::name()
{
	return m_name;
}

const std::string& CommandlineCommand::description()
{
	return m_description;
}

void CommandlineCommand::printHelp()
{
	// Overridden by each command to render its Glaze-generated option help.
}

}	 // namespace commandline
