#ifndef COMMANDLINE_COMMAND_CONFIG_H
#define COMMANDLINE_COMMAND_CONFIG_H

#include <optional>

#include "CommandlineCommand.h"

namespace commandline
{
class CommandlineCommandConfig: public CommandlineCommand
{
public:
	CommandlineCommandConfig(CommandLineParser* parser);
	~CommandlineCommandConfig() override;

	void setup() override;
	ReturnStatus parse(std::vector<std::string>& args) override;

	bool hasHelp() const override
	{
		return true;
	}

private:
	std::optional<int> m_indexerThreads;
	std::optional<bool> m_loggingEnabled;
	std::optional<bool> m_verboseIndexerLogging;
	std::vector<std::string> m_globalHeaderSearchPaths;
	std::vector<std::string> m_globalFrameworkSearchPaths;
	bool m_show = false;
};

}	 // namespace commandline

#endif	  // COMMANDLINE_COMMAND_CONFIG_H
