#ifndef EDITOR_COMMAND_H
#define EDITOR_COMMAND_H

#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "FilePath.h"

// Opens a file at a line and column in the person's editor, without an IDE plugin: a
// command with the placeholders {file}, {line} and {column} (the preference "Editor
// command"), e.g. `code -g {file}:{line}:{column}`. The command is split into arguments
// first and the placeholders are filled into them after, and it runs without a shell: a
// file name cannot add arguments or commands.
namespace editor_command
{
enum class EditorCommandError
{
	Empty,	// no command, and none detected: the OS opens the file
	UnbalancedQuote,
	NotFound,	// the program is not on the PATH
	StartFailed
};
std::string toString(EditorCommandError error);

// The command's words, as a shell would split them, without its expansions: blanks
// separate; '...' is literal; "..." keeps blanks, with \" and \\ as escapes. A backslash
// elsewhere is literal (a Windows path).
std::expected<std::vector<std::string>, EditorCommandError> split(std::string_view command);

// The program and its arguments for `file` at `line` and `column` (1-based): the
// placeholders filled in per argument; without {file}, the file is the last argument.
std::expected<std::vector<std::string>, EditorCommandError> expand(
	std::string_view command, const FilePath& file, std::size_t line, std::size_t column);

// The command for an editor program (by its file name: code, subl, zed, ...), or empty
// for one this does not know. Terminal editors (vim, nano, emacs, ...) are not known:
// they need a terminal, and Sourcetrail does not start one.
std::string commandForEditor(std::string_view program);

// The command for the editor this machine has: $VISUAL or $EDITOR when it is a GUI editor
// this knows, else the first of code, cursor, zed, subl, mate found on the PATH (and the
// places GUI apps are installed to, which a Finder-started app's PATH leaves out). Empty
// when none is found: the OS opens the file with its default app.
std::string detectCommand();

// The command to run: `configured` unless it is empty, else detectCommand().
std::string effectiveCommand(const std::string& configured);

// Starts the editor on `file` (detached), or has the OS open it when `command` is empty.
// Returns how it was opened, for the status bar.
std::expected<std::string, EditorCommandError> open(
	const std::string& command, const FilePath& file, std::size_t line, std::size_t column);
}	 // namespace editor_command

#endif	  // EDITOR_COMMAND_H
