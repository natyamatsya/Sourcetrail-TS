#include "CommandLineParser.h"

#include <iostream>
#include <span>

#include "CommandlineCommandConfig.h"
#include "CommandlineCommandIndex.h"
#include "CommandlineCommandMerge.h"
#include "ConfigManager.h"
#include "GlazeCli.h"
#include "ProjectSettings.h"
#include "TextAccess.h"

namespace glz
{
template <>
struct meta<commandline::TopOpts>
{
	using T = commandline::TopOpts;
	static constexpr auto value = object("version", &T::version, "project-file", &T::project_file,
		"screenshot", &T::screenshot, "screenshot-delay", &T::screenshot_delay, "ui-snapshot",
		&T::ui_snapshot, "ui-snapshot-format", &T::ui_snapshot_format, "agent-instance",
		&T::agent_instance);
};
}	 // namespace glz

namespace commandline
{
CommandLineParser::CommandLineParser(const std::string& version) : m_version(version)
{
	m_commands.push_back(std::make_unique<commandline::CommandlineCommandConfig>(this));
	m_commands.push_back(std::make_unique<commandline::CommandlineCommandIndex>(this));
	m_commands.push_back(std::make_unique<commandline::CommandlineCommandMerge>(this));

	for (auto& command: m_commands)
		command->setup();
}

CommandLineParser::~CommandLineParser() = default;

void CommandLineParser::preparse(int argc, char** argv)
{
	std::vector<std::string> args;
	for (int i = 1; i < argc; i++)
		args.push_back(std::string(argv[i]));

	preparse(args);
}

void CommandLineParser::preparse(std::vector<std::string>& args)
{
	if (args.empty())
		return;

	m_args = args;

	for (auto& command: m_commands)
	{
		if (m_args[0] == command->name())
		{
			m_withoutGUI = true;
			return;
		}
	}

	// Top-level options tolerate unknown tokens (allow_extras): a subcommand may
	// follow, and its options are parsed later in parse().
	const glzcli::ParseResult result =
		glzcli::parse(m_top, std::span<const std::string>(m_args), /*allow_extras=*/true);

	// --version wins over a trailing bare "help" token ("--version help" prints
	// the version, not the help text); the parser stops at the help token, so
	// only options preceding it are set.
	if (m_top.version)
	{
		std::cout << "Sourcetrail Version " << m_version << std::endl;
		m_quit = true;
		return;
	}

	if (result.help)
	{
		printHelp();
		m_quit = true;
		return;
	}
	if (result.error)
	{
		std::cerr << "ERROR: " << *result.error << std::endl;
		return;
	}

	if (!m_top.project_file.empty())
	{
		m_projectFile = FilePath(m_top.project_file);
		processProjectfile();
	}
}

void CommandLineParser::parse()
{
	if (m_args.empty())
		return;

	for (auto& command: m_commands)
	{
		if (m_args[0] == command->name())
		{
			m_args.erase(m_args.begin());
			const CommandlineCommand::ReturnStatus status = command->parse(m_args);

			if (status != CommandlineCommand::ReturnStatus::CMD_OK)
				m_quit = true;
		}
	}
}

void CommandLineParser::setProjectFile(const FilePath& filepath)
{
	m_projectFile = filepath;
	processProjectfile();
}

void CommandLineParser::printHelp() const
{
	std::cout << "Usage:\n  Sourcetrail [command] [option...] [positional arguments]\n\n";

	std::cout << "Commands:\n";
	for (const auto& command: m_commands)
	{
		std::cout << "  " << command->name();
		std::cout << std::string(std::max(23 - command->name().size(), size_t(2)), ' ');
		std::cout << command->description() << (command->hasHelp() ? "*" : "") << "\n";
	}
	std::cout << "\n  * has its own --help\n\n";

	std::cout << "Options:\n" << glzcli::help<TopOpts>() << std::endl;
}

bool CommandLineParser::runWithoutGUI() const
{
	return m_withoutGUI;
}

bool CommandLineParser::exitApplication() const
{
	return m_quit;
}

const std::string& CommandLineParser::getScreenshotPath() const
{
	return m_top.screenshot;
}

int CommandLineParser::getScreenshotDelayMs() const
{
	return m_top.screenshot_delay;
}

const std::string& CommandLineParser::getUiSnapshotPath() const
{
	return m_top.ui_snapshot;
}

const std::string& CommandLineParser::getUiSnapshotFormat() const
{
	return m_top.ui_snapshot_format;
}

const std::string& CommandLineParser::getAgentInstanceId() const
{
	return m_top.agent_instance;
}

bool CommandLineParser::hasError() const
{
	return !m_errorString.empty();
}

std::string CommandLineParser::getError()
{
	return m_errorString;
}

void CommandLineParser::processProjectfile()
{
	m_projectFile.makeAbsolute();

	const std::string errorstring =
		"Provided Projectfile is not valid:\n* Provided Projectfile('" + m_projectFile.fileName() +
		"') ";
	if (!m_projectFile.exists())
	{
		m_errorString = errorstring + " does not exist";
		m_projectFile = FilePath();
		return;
	}

	if (!ProjectSettings::isProjectFilePath(m_projectFile))
	{
		m_errorString = errorstring + " has a wrong file ending";
		m_projectFile = FilePath();
		return;
	}

	std::shared_ptr<ConfigManager> configManager = ConfigManager::createEmpty();
	if (!configManager->load(TextAccess::createFromFile(m_projectFile)))
	{
		m_errorString = errorstring + " could not be loaded (invalid)";
		m_projectFile = FilePath();
		return;
	}
}

void CommandLineParser::fullRefresh()
{
	m_refreshMode = RefreshMode::ALL_FILES;
}

void CommandLineParser::incompleteRefresh()
{
	m_refreshMode = RefreshMode::UPDATED_AND_INCOMPLETE_FILES;
}

const FilePath& CommandLineParser::getProjectFilePath() const
{
	return m_projectFile;
}

const ShardConfig& CommandLineParser::getShardConfig() const
{
	return m_shardConfig;
}

void CommandLineParser::setShardConfig(const ShardConfig& config)
{
	m_shardConfig = config;
}

RefreshMode CommandLineParser::getRefreshMode() const
{
	return m_refreshMode;
}

}	 // namespace commandline
