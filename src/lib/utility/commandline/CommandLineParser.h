#ifndef COMMAND_LINE_PARSER_H
#define COMMAND_LINE_PARSER_H

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "FilePath.h"
#include "RefreshInfo.h"
#include "ShardConfig.h"

namespace commandline
{
class CommandlineCommand;

//! Top-level (GUI) options: everything before a subcommand name. See the
//! glz::meta specialization in CommandLineParser.cpp for the flag mapping.
struct TopOpts
{
	bool version = false;
	std::string project_file;
	std::string screenshot;
	int screenshot_delay = 2000;
	std::string ui_snapshot;
	std::string ui_snapshot_format = "accessibility";
	std::string agent_instance;

	static constexpr std::array shorts = {std::pair{'v', std::string_view{"version"}}};
	static constexpr std::array<std::string_view, 1> positionals = {"project-file"};
	static constexpr std::array descriptions = {
		std::pair{std::string_view{"version"}, std::string_view{"Print the Sourcetrail version and exit"}},
		std::pair{std::string_view{"project-file"}, std::string_view{"Open Sourcetrail with this project (.srctrl.toml)"}},
		std::pair{std::string_view{"screenshot"},
			std::string_view{"Headless: run the GUI (forced offscreen unless QT_QPA_PLATFORM is set), capture the main window to this PNG path, then exit"}},
		std::pair{std::string_view{"screenshot-delay"},
			std::string_view{"Delay in ms before the --screenshot/--ui-snapshot capture (default 2000)"}},
		std::pair{std::string_view{"ui-snapshot"},
			std::string_view{"Headless: dump the live Qt widget tree to this path, then exit (see context/DESIGN_AGENT_UI_SNAPSHOT.md)"}},
		std::pair{std::string_view{"ui-snapshot-format"},
			std::string_view{"Snapshot format: 'accessibility' (default) or 'object'"}},
		std::pair{std::string_view{"agent-instance"},
			std::string_view{"Namespace the agent-control channels as st.agent.<id>.* for side-by-side instances (empty = default)"}}};
};

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

	// Headless UI-snapshot dump (widget tree -> JSON). See DESIGN_AGENT_UI_SNAPSHOT.md.
	const std::string& getUiSnapshotPath() const;
	const std::string& getUiSnapshotFormat() const;

	// Instance id namespacing the agent-control channels (empty = default). The
	// channel itself is always active in agent builds (SOURCETRAIL_AGENT_CONTROL).
	const std::string& getAgentInstanceId() const;

private:
	void processProjectfile();
	void printHelp() const;

	std::vector<std::shared_ptr<CommandlineCommand>> m_commands;
	std::vector<std::string> m_args;

	const std::string m_version;
	TopOpts m_top;
	FilePath m_projectFile;
	RefreshMode m_refreshMode = RefreshMode::UPDATED_FILES;
	ShardConfig m_shardConfig;

	bool m_quit = false;
	bool m_withoutGUI = false;

	std::string m_errorString;
};

}	 // namespace commandline

#endif	  // COMMAND_LINE_PARSER_H
