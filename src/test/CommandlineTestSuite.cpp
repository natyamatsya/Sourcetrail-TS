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

	ApplicationSettings::getInstance()->load(appSettingsPath);
}
