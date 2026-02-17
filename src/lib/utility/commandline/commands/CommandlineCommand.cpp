#include "CommandlineCommand.h"

#include <iostream>

namespace commandline
{
CommandlineCommand::CommandlineCommand(
	const std::string& name, const std::string& description, CommandLineParser* parser)
	: m_name(name), m_description(description), m_parser(parser), m_app(description)
{
	m_app.name(name);
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
	std::cout << m_app.help() << std::endl;
}

}	 // namespace commandline
