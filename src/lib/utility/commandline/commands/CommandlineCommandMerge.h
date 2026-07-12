#ifndef COMMANDLINE_COMMAND_MERGE_H
#define COMMANDLINE_COMMAND_MERGE_H

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "CommandlineCommand.h"

namespace commandline
{
struct MergeOpts
{
	std::string project_file;
	std::vector<std::string> shard_dbs;
	std::string output;
	bool allow_partial = false;

	static constexpr std::array shorts = {std::pair{'o', std::string_view{"output"}}};
	static constexpr std::array<std::string_view, 2> positionals = {"project-file", "shard-dbs"};
	static constexpr std::array descriptions = {
		std::pair{std::string_view{"project-file"},
			std::string_view{"Project file the shards were produced from (.srctrl.toml)"}},
		std::pair{std::string_view{"shard-dbs"}, std::string_view{"Shard DB files to merge (2 or more)"}},
		std::pair{std::string_view{"output"}, std::string_view{"Output DB path (default: the project's DB)"}},
		std::pair{std::string_view{"allow-partial"},
			std::string_view{"Merge even if the shard set is incomplete/inconsistent"}}};
};

//! Combines shard DBs produced by `index --full --shard i/N` into one project DB.
//! Runs synchronously (no project load, no indexing); see ShardConfig.h for the
//! producer side and context/INDEXING_OPTIMIZATIONS.md for constraints.
class CommandlineCommandMerge: public CommandlineCommand
{
public:
	CommandlineCommandMerge(CommandLineParser* parser);
	~CommandlineCommandMerge() override;

	void setup() override;
	ReturnStatus parse(std::vector<std::string>& args) override;
	void printHelp() override;

	bool hasHelp() const override
	{
		return true;
	}

private:
	ReturnStatus merge() const;

	MergeOpts m_opts;
};

}	 // namespace commandline

#endif	  // COMMANDLINE_COMMAND_MERGE_H
