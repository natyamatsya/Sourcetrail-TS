#include "CommandLineParser.h"

#include <iostream>

#include "CommandlineCommandConfig.h"
#include "CommandlineCommandIndex.h"
#include "CommandlineCommandMerge.h"
#include "ConfigManager.h"
#include "ProjectSettings.h"
#include "TextAccess.h"

namespace commandline
{
CommandLineParser::CommandLineParser(const std::string& version)
	: m_app("Sourcetrail"), m_version(version)
{
	m_app.add_flag("-v,--version", "Version of Sourcetrail");
	m_app.add_option(
		"project-file",
		m_projectFileArg,
		"Open Sourcetrail with this project (.srctrl.toml)");
	m_app.add_option(
		"--screenshot",
		m_screenshotPath,
		"Headless: run the GUI (forced offscreen unless QT_QPA_PLATFORM is set), "
		"capture the main window to this PNG path, then exit");
	m_app.add_option(
		"--screenshot-delay",
		m_screenshotDelayMs,
		"Delay in ms before the --screenshot capture, to let a project render (default 2000)");
	m_app.add_flag(
		"--agent-control,!--no-agent-control",
		m_agentControl,
		"Agent-UI control channel (thoth-ipc). On by default; pass --no-agent-control to "
		"disable. No-op unless built with SOURCETRAIL_AGENT_CONTROL");
	m_app.allow_extras();

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

	try
	{
		for (auto& command: m_commands)
		{
			if (m_args[0] == command->name())
			{
				m_withoutGUI = true;
				return;
			}
		}

		m_app.parse(m_args);

		if (m_app.count("--version"))
		{
			std::cout << "Sourcetrail Version " << m_version << std::endl;
			m_quit = true;
			return;
		}

		if (m_app.count("--help"))
		{
			printHelp();
			m_quit = true;
		}

		if (!m_projectFileArg.empty())
		{
			m_projectFile = FilePath(m_projectFileArg);
			processProjectfile();
		}
	}
	catch (const CLI::ParseError& e)
	{
		if (e.get_exit_code() == 0)
		{
			// --help was invoked
			printHelp();
			m_quit = true;
		}
		else
		{
			std::cerr << "ERROR: " << e.what() << std::endl;
		}
	}
}

void CommandLineParser::parse()
{
	if (m_args.empty())
		return;

	try
	{
		for (auto& command: m_commands)
		{
			if (m_args[0] == command->name())
			{
				m_args.erase(m_args.begin());
				CommandlineCommand::ReturnStatus status = command->parse(m_args);

				if (status != CommandlineCommand::ReturnStatus::CMD_OK)
					m_quit = true;
			}
		}
	}
	catch (const CLI::ParseError& e)
	{
		std::cerr << "ERROR: " << e.what() << std::endl;
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

	std::cout << m_app.help() << std::endl;
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
	return m_screenshotPath;
}

int CommandLineParser::getScreenshotDelayMs() const
{
	return m_screenshotDelayMs;
}

bool CommandLineParser::getAgentControlEnabled() const
{
	return m_agentControl;
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
