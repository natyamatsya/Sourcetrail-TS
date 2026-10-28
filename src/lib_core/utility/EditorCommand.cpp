#include "EditorCommand.h"

#include <array>
#include <cstdlib>
#include <optional>

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>

namespace editor_command
{
namespace
{
struct KnownEditor
{
	std::string_view program;
	std::string_view command;
};

// GUI editors, by program name, and how they open a file at a line and column.
constexpr std::array knownEditors {
	KnownEditor {"code", "code -g {file}:{line}:{column}"},
	KnownEditor {"code-insiders", "code-insiders -g {file}:{line}:{column}"},
	KnownEditor {"codium", "codium -g {file}:{line}:{column}"},
	KnownEditor {"cursor", "cursor -g {file}:{line}:{column}"},
	KnownEditor {"windsurf", "windsurf -g {file}:{line}:{column}"},
	KnownEditor {"zed", "zed {file}:{line}:{column}"},
	KnownEditor {"subl", "subl {file}:{line}:{column}"},
	KnownEditor {"sublime_text", "sublime_text {file}:{line}:{column}"},
	KnownEditor {"mate", "mate -l {line}:{column} {file}"},
	KnownEditor {"gvim", "gvim +{line} {file}"},
	KnownEditor {"mvim", "mvim +{line} {file}"},
	KnownEditor {"kate", "kate --line {line} --column {column} {file}"},
	KnownEditor {"gedit", "gedit +{line}:{column} {file}"},
	KnownEditor {"emacsclient", "emacsclient -n +{line}:{column} {file}"},
	KnownEditor {"clion", "clion --line {line} --column {column} {file}"},
	KnownEditor {"idea", "idea --line {line} --column {column} {file}"},
	KnownEditor {"notepad++", "notepad++ -n{line} -c{column} {file}"}};

// Looked for on the PATH, in this order, when $VISUAL and $EDITOR name none of them.
constexpr std::array detectedPrograms {"code", "cursor", "zed", "subl", "mate"};

// Where GUI editors put their command line tools: an app started from the Finder or a
// desktop launcher has a PATH without them.
QStringList extraProgramDirectories()
{
	QStringList directories {
		QStringLiteral("/usr/local/bin"),
		QStringLiteral("/opt/homebrew/bin"),
		QDir::homePath() + QStringLiteral("/.local/bin"),
		QDir::homePath() + QStringLiteral("/bin")};
#if defined(Q_OS_MACOS)
	directories << QStringLiteral("/Applications/Visual Studio Code.app/Contents/Resources/app/bin")
				<< QStringLiteral("/Applications/Cursor.app/Contents/Resources/app/bin")
				<< QStringLiteral("/Applications/Sublime Text.app/Contents/SharedSupport/bin");
#endif
	return directories;
}

// The program's path: as given when it has one, else found on the PATH or in the extra
// directories.
std::optional<QString> findProgram(const QString& program)
{
	if (QFileInfo(program).isAbsolute())
	{
		return QFileInfo(program).isExecutable() ? std::optional(program) : std::nullopt;
	}
	QString found = QStandardPaths::findExecutable(program);
	if (found.isEmpty())
	{
		found = QStandardPaths::findExecutable(program, extraProgramDirectories());
	}
	return found.isEmpty() ? std::nullopt : std::optional(found);
}

std::string programName(std::string_view program)
{
	std::string name = QFileInfo(QString::fromUtf8(program.data(), static_cast<qsizetype>(program.size())))
						   .fileName()
						   .toStdString();
	if (name.ends_with(".exe") || name.ends_with(".cmd") || name.ends_with(".bat"))
	{
		name.resize(name.size() - 4);
	}
	return name;
}

void replaceAll(std::string& text, std::string_view placeholder, const std::string& value)
{
	for (std::size_t position = text.find(placeholder); position != std::string::npos;
		 position = text.find(placeholder, position + value.size()))
	{
		text.replace(position, placeholder.size(), value);
	}
}
}	 // namespace

std::string toString(EditorCommandError error)
{
	switch (error)
	{
	case EditorCommandError::Empty:
		return "the editor command is empty";
	case EditorCommandError::UnbalancedQuote:
		return "the editor command has a quote that is not closed";
	case EditorCommandError::NotFound:
		return "the editor program was not found";
	case EditorCommandError::StartFailed:
		return "the editor did not start";
	}
	return "unknown editor command error";
}

std::expected<std::vector<std::string>, EditorCommandError> split(std::string_view command)
{
	std::vector<std::string> words;
	std::string word;
	bool inWord = false;
	char quote = 0;
	for (std::size_t i = 0; i < command.size(); i++)
	{
		const char c = command[i];
		if (quote == '\'')
		{
			if (c == '\'')
			{
				quote = 0;
			}
			else
			{
				word.push_back(c);
			}
		}
		else if (quote == '"')
		{
			if (c == '"')
			{
				quote = 0;
			}
			else if (c == '\\' && i + 1 < command.size() && (command[i + 1] == '"' || command[i + 1] == '\\'))
			{
				word.push_back(command[++i]);
			}
			else
			{
				word.push_back(c);
			}
		}
		else if (c == ' ' || c == '\t' || c == '\n')
		{
			if (inWord)
			{
				words.push_back(std::move(word));
				word.clear();
				inWord = false;
			}
		}
		else
		{
			inWord = true;
			if (c == '\'' || c == '"')
			{
				quote = c;
			}
			else
			{
				word.push_back(c);
			}
		}
	}
	if (quote != 0)
	{
		return std::unexpected(EditorCommandError::UnbalancedQuote);
	}
	if (inWord)
	{
		words.push_back(std::move(word));
	}
	if (words.empty())
	{
		return std::unexpected(EditorCommandError::Empty);
	}
	return words;
}

std::expected<std::vector<std::string>, EditorCommandError> expand(
	std::string_view command, const FilePath& file, std::size_t line, std::size_t column)
{
	return split(command).transform([&](std::vector<std::string> words) {
		bool hasFile = false;
		for (std::string& word: words)
		{
			hasFile = hasFile || word.find("{file}") != std::string::npos;
			replaceAll(word, "{file}", file.str());
			replaceAll(word, "{line}", std::to_string(std::max<std::size_t>(line, 1)));
			replaceAll(word, "{column}", std::to_string(std::max<std::size_t>(column, 1)));
		}
		if (!hasFile)
		{
			words.push_back(file.str());
		}
		return words;
	});
}

std::string commandForEditor(std::string_view program)
{
	const std::string name = programName(program);
	for (const KnownEditor& editor: knownEditors)
	{
		if (editor.program == name)
		{
			// The program as it was named (it may be a path), with the editor's arguments.
			std::string command(editor.command);
			const std::string quoted = std::string(program).find_first_of(" \t") == std::string::npos
				? std::string(program)
				: "\"" + std::string(program) + "\"";
			command.replace(0, editor.program.size(), quoted);
			return command;
		}
	}
	return {};
}

std::string detectCommand()
{
	for (const char* variable: {"VISUAL", "EDITOR"})
	{
		if (const char* value = std::getenv(variable); value != nullptr && *value != 0)
		{
			// Its first word is the program; an editor's own flags in it are left out.
			if (const auto words = split(value); words)
			{
				std::string command = commandForEditor(words->front());
				if (!command.empty() && findProgram(QString::fromStdString(words->front())))
				{
					return command;
				}
			}
		}
	}

	for (const char* program: detectedPrograms)
	{
		if (const std::optional<QString> path = findProgram(QString::fromUtf8(program)))
		{
			// By its full path: the PATH the editor is started with may not have it.
			return commandForEditor(path->toStdString());
		}
	}
	return {};
}

std::string effectiveCommand(const std::string& configured)
{
	return configured.find_first_not_of(" \t") == std::string::npos ? detectCommand() : configured;
}

std::expected<std::string, EditorCommandError> open(
	const std::string& command, const FilePath& file, std::size_t line, std::size_t column)
{
	if (command.find_first_not_of(" \t") == std::string::npos)
	{
		if (!QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(file.str()))))
		{
			return std::unexpected(EditorCommandError::StartFailed);
		}
		return std::string("the default app");
	}

	return expand(command, file, line, column)
		.and_then([](const std::vector<std::string>& words) -> std::expected<std::string, EditorCommandError> {
			const std::optional<QString> program = findProgram(QString::fromStdString(words.front()));
			if (!program)
			{
				return std::unexpected(EditorCommandError::NotFound);
			}
			QStringList arguments;
			for (std::size_t i = 1; i < words.size(); i++)
			{
				arguments << QString::fromStdString(words[i]);
			}
			if (!QProcess::startDetached(*program, arguments))
			{
				return std::unexpected(EditorCommandError::StartFailed);
			}
			return programName(words.front());
		});
}
}	 // namespace editor_command
