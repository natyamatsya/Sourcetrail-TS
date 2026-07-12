#ifndef COMMANDLINE_COMMAND_CONFIG_H
#define COMMANDLINE_COMMAND_CONFIG_H

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "CommandlineCommand.h"

namespace commandline
{
struct ConfigOpts
{
	std::optional<int> indexer_threads;
	std::optional<bool> logging_enabled;
	std::optional<bool> verbose_indexer_logging_enabled;
	std::vector<std::string> global_header_search_paths;
	std::vector<std::string> global_framework_search_paths;
	bool show = false;

	static constexpr std::array shorts = {std::pair{'t', std::string_view{"indexer-threads"}},
		std::pair{'l', std::string_view{"logging-enabled"}},
		std::pair{'L', std::string_view{"verbose-indexer-logging-enabled"}},
		std::pair{'g', std::string_view{"global-header-search-paths"}},
		std::pair{'F', std::string_view{"global-framework-search-paths"}}, std::pair{'s', std::string_view{"show"}}};
	static constexpr std::array descriptions = {
		std::pair{std::string_view{"indexer-threads"}, std::string_view{"Number of indexing threads (0 = ideal count)"}},
		std::pair{std::string_view{"logging-enabled"}, std::string_view{"Enable file/console logging <true/false>"}},
		std::pair{std::string_view{"verbose-indexer-logging-enabled"}, std::string_view{"Log the AST during indexing <true/false> (slow)"}},
		std::pair{std::string_view{"global-header-search-paths"}, std::string_view{"Global include paths (repeat or comma-separated)"}},
		std::pair{std::string_view{"global-framework-search-paths"}, std::string_view{"Global framework paths (repeat or comma-separated)"}},
		std::pair{std::string_view{"show"}, std::string_view{"Display all settings"}}};
};

class CommandlineCommandConfig: public CommandlineCommand
{
public:
	CommandlineCommandConfig(CommandLineParser* parser);
	~CommandlineCommandConfig() override;

	void setup() override;
	ReturnStatus parse(std::vector<std::string>& args) override;
	void printHelp() override;

	bool hasHelp() const override
	{
		return true;
	}

private:
	ConfigOpts m_opts;
};

}	 // namespace commandline

#endif	  // COMMANDLINE_COMMAND_CONFIG_H
