#include "Catch2.hpp"

#ifndef SRCTRL_MODULE_BUILD
#include "ApplicationSettings.h"
#endif
#include "CommandLineParser.h"

#include <iostream>
#include <sstream>
#include <string>

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.settings;
#endif

TEST_CASE("command line")
{
	FilePath appSettingsPath = ApplicationSettings::getInstance()->getFilePath();
	ApplicationSettings::getInstance()->load(FilePath("data/CommandlineTestSuite/settings.xml"));

	SECTION("commandline version")
	{
		std::vector<std::string> args({"--version", "help"});

		std::stringstream redStream;
		auto *oldBuf = std::cout.rdbuf(redStream.rdbuf());

		commandline::CommandLineParser parser("2016.1");
		parser.preparse(args);
		parser.parse();

		std::cout.rdbuf(oldBuf);

		REQUIRE(redStream.str() == "Sourcetrail Version 2016.1\n");
	}

	SECTION("command config help") {}

	SECTION("command config filepathVector")
	{
		std::vector<std::string> args(
			{"config", "-g", "/usr", "-g", "/usr/share/include", "-g", "/opt/test/include"});

		commandline::CommandLineParser parser("2");
		parser.preparse(args);
		parser.parse();

		std::vector<FilePath> paths = ApplicationSettings::getInstance()->getHeaderSearchPaths();
		REQUIRE(paths[0].str() == "/usr");
		REQUIRE(paths[1].str() == "/usr/share/include");
		REQUIRE(paths[2].str() == "/opt/test/include");
	}

	SECTION("command config filepathVector comma separated")
	{
		std::vector<std::string> args(
			{"config", "--global-header-search-paths", "/usr,/usr/include,/include,/opt/include"});

		commandline::CommandLineParser parser("2");
		parser.preparse(args);
		parser.parse();

		std::vector<FilePath> paths = ApplicationSettings::getInstance()->getHeaderSearchPaths();
		REQUIRE(paths[0].str() == "/usr");
		REQUIRE(paths[1].str() == "/usr/include");
		REQUIRE(paths[2].str() == "/include");
		REQUIRE(paths[3].str() == "/opt/include");
	}

	SECTION("command index without a project file is an error, not a run")
	{
		std::vector<std::string> args({"index", "--full"});

		commandline::CommandLineParser parser("2");
		parser.preparse(args);
		parser.parse();

		REQUIRE(parser.runWithoutGUI());
		REQUIRE_FALSE(parser.exitApplication());
		REQUIRE(parser.hasError());
		REQUIRE(parser.getError().find("No project file given") != std::string::npos);
	}

	SECTION("a command's failure is the process's exit code, its help is not")
	{
		std::stringstream redStream;
		auto* oldOut = std::cout.rdbuf(redStream.rdbuf());
		auto* oldErr = std::cerr.rdbuf(redStream.rdbuf());

		std::vector<std::string> failing({"index", "--no-such-option"});
		commandline::CommandLineParser failed("2");
		failed.preparse(failing);
		failed.parse();

		std::vector<std::string> help({"index", "--help"});
		commandline::CommandLineParser helped("2");
		helped.preparse(help);
		helped.parse();

		std::cout.rdbuf(oldOut);
		std::cerr.rdbuf(oldErr);

		REQUIRE(failed.exitApplication());
		REQUIRE(failed.exitCode() == 1);
		REQUIRE(helped.exitApplication());
		REQUIRE(helped.exitCode() == 0);
	}

	ApplicationSettings::getInstance()->load(appSettingsPath);
}
