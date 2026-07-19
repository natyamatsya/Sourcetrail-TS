#ifndef COMMANDLINE_COMMAND_INDEX_H
#define COMMANDLINE_COMMAND_INDEX_H

#include <array>
#include <string>
#include <string_view>
#include <utility>

#include "CommandlineCommand.h"

namespace commandline
{
struct IndexOpts
{
	bool incomplete = false;
	bool full = false;
	std::string shard;
	std::string shard_output;
	std::string project_file;

	static constexpr std::array shorts = {
		std::pair{'i', std::string_view{"incomplete"}}, std::pair{'f', std::string_view{"full"}}};
	static constexpr std::array<std::string_view, 1> positionals = {"project-file"};
	static constexpr std::array descriptions = {
		std::pair{std::string_view{"incomplete"}, std::string_view{"Also reindex incomplete files (files with errors)"}},
		std::pair{std::string_view{"full"}, std::string_view{"Index full project (omit to only index new/changed files)"}},
		std::pair{std::string_view{"shard"}, std::string_view{"Distributed indexing: index only the i-th of N stripes (i/N, 1-based; requires --full). Merge with the 'merge' command"}},
		std::pair{std::string_view{"shard-output"}, std::string_view{"Output path for the shard DB (default: <project>.shard<i>of<N>.srctrl.db)"}},
		std::pair{std::string_view{"project-file"}, std::string_view{"Project file to index (.srctrl.toml)"}}};
};

class CommandlineCommandIndex: public CommandlineCommand
{
public:
	CommandlineCommandIndex(CommandLineParser* parser);
	~CommandlineCommandIndex() override;

	void setup() override;
	ReturnStatus parse(std::vector<std::string>& args) override;
	void printHelp() override;

	bool hasHelp() const override
	{
		return true;
	}

private:
	IndexOpts m_opts;
};

}	 // namespace commandline

#endif	  // COMMANDLINE_COMMAND_INDEX_H
