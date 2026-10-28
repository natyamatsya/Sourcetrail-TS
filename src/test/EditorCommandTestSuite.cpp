#include "Catch2.hpp"

#include "EditorCommand.h"

using editor_command::EditorCommandError;

TEST_CASE("editor command splits into words as a shell would, without expanding")
{
	using Words = std::vector<std::string>;
	REQUIRE(editor_command::split("code -g {file}:{line}") == Words {"code", "-g", "{file}:{line}"});
	REQUIRE(editor_command::split("  subl\t{file}  ") == Words {"subl", "{file}"});
	REQUIRE(
		editor_command::split(R"("/Applications/Visual Studio Code.app/bin/code" -g {file})") ==
		Words {"/Applications/Visual Studio Code.app/bin/code", "-g", "{file}"});
	REQUIRE(editor_command::split(R"(ed 'it''s' "say \"hi\"")") == Words {"ed", "its", R"(say "hi")"});
	REQUIRE(editor_command::split(R"(C:\Tools\notepad++.exe -n{line} {file})") ==
		Words {R"(C:\Tools\notepad++.exe)", "-n{line}", "{file}"});
	REQUIRE(editor_command::split("ed $(rm -rf ~) ; ls") == Words {"ed", "$(rm", "-rf", "~)", ";", "ls"});

	REQUIRE(editor_command::split("").error() == EditorCommandError::Empty);
	REQUIRE(editor_command::split("   ").error() == EditorCommandError::Empty);
	REQUIRE(editor_command::split(R"(code "unclosed)").error() == EditorCommandError::UnbalancedQuote);
}

TEST_CASE("editor command fills the placeholders into the words")
{
	using Words = std::vector<std::string>;
	const FilePath file("/src/my project/a;b $(x).h");

	// The file stays one argument whatever it is called: nothing runs it through a shell.
	REQUIRE(editor_command::expand("code -g {file}:{line}:{column}", file, 12, 3) ==
		Words {"code", "-g", "/src/my project/a;b $(x).h:12:3"});
	REQUIRE(editor_command::expand("mate -l {line} {file}", file, 7, 1) == Words {"mate", "-l", "7", file.str()});
	// Without {file}, the file comes last; lines and columns are at least 1.
	REQUIRE(editor_command::expand("myeditor --wait", file, 0, 0) == Words {"myeditor", "--wait", file.str()});
	REQUIRE(editor_command::expand("e +{line}:{column}:{line} {file}", file, 5, 2) ==
		Words {"e", "+5:2:5", file.str()});
	REQUIRE(editor_command::expand("", file, 1, 1).error() == EditorCommandError::Empty);
}

TEST_CASE("editor command knows the GUI editors, not the terminal ones")
{
	REQUIRE(editor_command::commandForEditor("code") == "code -g {file}:{line}:{column}");
	REQUIRE(editor_command::commandForEditor("/opt/homebrew/bin/subl") == "/opt/homebrew/bin/subl {file}:{line}:{column}");
	REQUIRE(editor_command::commandForEditor("/Applications/Visual Studio Code.app/Contents/Resources/app/bin/code") ==
		R"("/Applications/Visual Studio Code.app/Contents/Resources/app/bin/code" -g {file}:{line}:{column})");
	REQUIRE(editor_command::commandForEditor("vim").empty());
	REQUIRE(editor_command::commandForEditor("nano").empty());

	// A detected command is one the configured setting stands in for.
	REQUIRE(editor_command::effectiveCommand("zed {file}") == "zed {file}");
	REQUIRE(editor_command::effectiveCommand("  ") == editor_command::detectCommand());
}
