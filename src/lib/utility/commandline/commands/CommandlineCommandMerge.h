#ifndef COMMANDLINE_COMMAND_MERGE_H
#define COMMANDLINE_COMMAND_MERGE_H

#include "CommandlineCommand.h"

namespace commandline
{
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

	bool hasHelp() const override
	{
		return true;
	}

private:
	ReturnStatus merge() const;

	std::string m_projectFileArg;
	std::vector<std::string> m_shardDbArgs;
	std::string m_outputArg;
	bool m_allowPartial = false;
};

}	 // namespace commandline

#endif	  // COMMANDLINE_COMMAND_MERGE_H
