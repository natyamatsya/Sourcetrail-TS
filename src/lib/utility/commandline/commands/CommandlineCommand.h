#ifndef COMMANDLINE_COMMAND_H
#define COMMANDLINE_COMMAND_H

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace glzcli
{
struct ParseResult;
}

namespace commandline
{
class CommandLineParser;

class CommandlineCommand
{
public:
	enum class ReturnStatus
	{
		CMD_OK,
		CMD_QUIT,
		CMD_FAILURE
	};

	CommandlineCommand(
		const std::string& name, const std::string& description, CommandLineParser* parser);
	virtual ~CommandlineCommand();

	const std::string& name();
	const std::string& description();

	virtual void setup() = 0;
	virtual ReturnStatus parse(std::vector<std::string>& args) = 0;

	virtual bool hasHelp() const = 0;
	virtual void printHelp();

protected:
	// Shared post-parse control flow, applied monadically at each command's
	// parse() entry: prints help on --help, prints the message on error, and
	// yields the matching early-exit status -- or std::nullopt to continue with a
	// good parse. Usage: `if (auto stop = earlyExit(result)) return *stop;`
	std::optional<ReturnStatus> earlyExit(const glzcli::ParseResult& result);

	const std::string m_name;
	const std::string m_description;
	CommandLineParser* m_parser;
};

}	 // namespace commandline

#endif	  // COMMANDLINE_COMMAND_H
