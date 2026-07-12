#ifndef COMMAND_LINE_PARSER_H
#define COMMAND_LINE_PARSER_H

#include <memory>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>

#include "FilePath.h"
#include "RefreshInfo.h"
#include "ShardConfig.h"

namespace commandline
{
class CommandlineCommand;

class CommandLineParser
{
public:
	CommandLineParser(const std::string& version);
	~CommandLineParser();

	void preparse(int argc, char** argv);
	void preparse(std::vector<std::string>& args);
	void parse();

	bool runWithoutGUI() const;
	bool exitApplication() const;

	bool hasError() const;
	std::string getError();

	void fullRefresh();
	void incompleteRefresh();

	const FilePath& getProjectFilePath() const;
	void setProjectFile(const FilePath& filepath);

	RefreshMode getRefreshMode() const;

	const ShardConfig& getShardConfig() const;
	void setShardConfig(const ShardConfig& config);

	// Agent/CI headless screenshot (see context/DESIGN_AGENT_UI_CONTROL.md).
	// When set, the GUI runs (forced to QT_QPA_PLATFORM=offscreen unless already
	// set) and captures its main window to this path after getScreenshotDelayMs().
	const std::string& getScreenshotPath() const;
	int getScreenshotDelayMs() const;

	// Activate the agent-UI control channel at startup (no-op unless the build has
	// SOURCETRAIL_AGENT_CONTROL). See context/DESIGN_AGENT_UI_CONTROL.md.
	bool getAgentControlEnabled() const;

	// Instance id namespacing the agent-control channels (empty = default).
	const std::string& getAgentInstanceId() const;

private:
	void processProjectfile();
	void printHelp() const;

	CLI::App m_app;

	std::vector<std::shared_ptr<CommandlineCommand>> m_commands;
	std::vector<std::string> m_args;

	const std::string m_version;
	std::string m_projectFileArg;
	FilePath m_projectFile;
	RefreshMode m_refreshMode = RefreshMode::UPDATED_FILES;
	ShardConfig m_shardConfig;

	bool m_quit = false;
	bool m_withoutGUI = false;

	std::string m_screenshotPath;
	int m_screenshotDelayMs = 2000;
	bool m_agentControl = true;
	std::string m_agentInstanceId;

	std::string m_errorString;
};

}	 // namespace commandline

#endif	  // COMMAND_LINE_PARSER_H
