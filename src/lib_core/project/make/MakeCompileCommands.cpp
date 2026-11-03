#include "MakeCompileCommands.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <iterator>
#include <format>
#include <optional>
#include <ranges>
#include <regex>
#include <set>
#include <span>

#include "FilePath.h"
#include "Platform.h"
#include "utilityApp.h"

namespace fs = std::filesystem;

namespace make
{
namespace
{
// A command line as the shell reads it: its words, and the operators between the commands
// (&&, ||, ;, |, &) as words of their own, marked.
struct Word
{
	std::string text;
	bool separator = false;
};

std::vector<Word> shellWords(std::string_view text)
{
	std::vector<Word> words;
	std::string word;
	bool inWord = false;
	const auto flush = [&]() {
		if (inWord)
		{
			words.push_back({std::move(word), false});
			word.clear();
			inWord = false;
		}
	};

	for (std::size_t i = 0; i < text.size(); ++i)
	{
		const char c = text[i];
		if (c == '\'')
		{
			inWord = true;
			const std::size_t end = text.find('\'', i + 1);
			word += text.substr(i + 1, (end == std::string_view::npos ? text.size() : end) - i - 1);
			i = end == std::string_view::npos ? text.size() : end;
		}
		else if (c == '"')
		{
			inWord = true;
			for (++i; i < text.size() && text[i] != '"'; ++i)
			{
				if (text[i] == '\\' && i + 1 < text.size() && std::string_view("\"\\$`").contains(text[i + 1]))
				{
					++i;
				}
				word += text[i];
			}
		}
		else if (c == '\\' && i + 1 < text.size())
		{
			inWord = true;
			word += text[++i];
		}
		else if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
		{
			flush();
		}
		else if (c == ';' || c == '|' || c == '&' || c == '(' || c == ')')
		{
			flush();
			std::string op(1, c);
			if ((c == '&' || c == '|') && i + 1 < text.size() && text[i + 1] == c)
			{
				op += text[++i];
			}
			words.push_back({std::move(op), true});
		}
		else
		{
			inWord = true;
			word += c;
		}
	}
	flush();
	return words;
}

std::string basename(const std::string& path)
{
	const std::size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string lower(std::string text)
{
	std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return text;
}

// gcc, g++, clang, clang++, cc, c++, cl, emcc ..., with a target prefix (aarch64-linux-gnu-gcc)
// or a version (clang-18, gcc-13) or .exe.
bool isCompiler(const std::string& program)
{
	static const std::regex compiler{
		R"(^(?:[A-Za-z0-9_.+]+(?:-[A-Za-z0-9_.+]+)*-)?(?:cc|c\+\+|gcc|g\+\+|clang|clang\+\+|clang-cl|cl|emcc|em\+\+|icc|icpc|icx|icpx|tcc)(?:-\d+(?:\.\d+)*)?(?:\.exe)?$)",
		std::regex::icase};
	return std::regex_match(basename(program), compiler);
}

bool isMake(const std::string& program)
{
	static const std::regex makeProgramName{R"(^(?:g|mingw32-|bmake|n)?make(?:\.exe)?$)", std::regex::icase};
	return std::regex_match(basename(program), makeProgramName);
}

// Programs that run the compiler they are given (ccache g++ ...) and the words of theirs before it.
bool isWrapper(const std::string& program)
{
	static const std::set<std::string> wrappers{
		"ccache", "sccache", "distcc", "icecc", "buildcache", "time", "nice", "env", "nohup", "xcrun"};
	return wrappers.contains(lower(basename(program)));
}

bool isAssignment(const std::string& word)
{
	static const std::regex assignment{R"(^[A-Za-z_][A-Za-z0-9_]*=)"};
	return std::regex_search(word, assignment);
}

bool isSourceFile(const std::string& path)
{
	static const std::set<std::string> extensions{
		".c", ".cc", ".cp", ".cpp", ".cxx", ".c++", ".m", ".mm", ".cu", ".ixx", ".cppm"};
	const std::string extension{fs::path(path).extension().string()};
	return extensions.contains(lower(extension)) || extension == ".C";
}

// Options whose value is the next word (-o out.o, -I dir ...).
bool takesValue(const std::string& option)
{
	static const std::set<std::string> options{
		"-o", "-I", "-D", "-U", "-include", "-imacros", "-isystem", "-iquote", "-idirafter", "-isysroot",
		"-iprefix", "-iwithprefix", "-iwithprefixbefore", "-iframework", "-MF", "-MT", "-MQ", "-x", "-arch",
		"-target", "-Xclang", "-Xpreprocessor", "-Xassembler", "-Xlinker", "-F", "-framework", "--sysroot",
		"-gcc-toolchain", "-ivfsoverlay", "-resource-dir", "-include-pch", "-aux-info", "-main-file-name",
		"-L", "-l", "-T", "-z", "-u", "-e", "--param", "-isystem-after", "-cxx-isystem"};
	return options.contains(option);
}

std::string withoutTrailingSlash(std::string path)
{
	while (path.size() > 1 && path.ends_with('/'))
	{
		path.pop_back();
	}
	return path;
}

// `path` against `directory`, normalized ("dir/." is "dir").
std::string joined(const fs::path& directory, const std::string& path)
{
	const fs::path p{path};
	return withoutTrailingSlash((p.is_absolute() ? p : directory / p).lexically_normal().generic_string());
}

// The files `pattern` names in `directory` when its file name has wildcards (src/*.c), as the shell
// expands them; the pattern itself when it has none, or matches nothing (so does the shell).
std::vector<std::string> expandGlob(const std::string& directory, const std::string& pattern)
{
	const fs::path path{pattern};
	const std::string name{path.filename().string()};
	if (name.find_first_of("*?[") == std::string::npos)
	{
		return {pattern};
	}
	std::string expression;
	for (const char c: name)
	{
		if (c == '*')
		{
			expression += "[^/]*";
		}
		else if (c == '?')
		{
			expression += "[^/]";
		}
		else if (c == '[' || c == ']' || c == '-')
		{
			expression += c;
		}
		else if (std::isalnum(static_cast<unsigned char>(c)) == 0)
		{
			expression += std::string("\\") + c;
		}
		else
		{
			expression += c;
		}
	}
	const std::regex matcher{expression};
	std::vector<std::string> files;
	std::error_code error;
	const fs::path parent{fs::path(directory) / path.parent_path()};
	for (fs::directory_iterator it{parent, error}, end; !error && it != end; it.increment(error))
	{
		const std::string file{it->path().filename().string()};
		if (!file.starts_with('.') && std::regex_match(file, matcher))
		{
			files.push_back((path.parent_path() / file).generic_string());
		}
	}
	std::ranges::sort(files);
	return files.empty() ? std::vector<std::string>{pattern} : files;
}

// One command of a line (the words between two separators) that compiles files (with -c, or
// compiling and linking them at once): its compile commands, one per file.
void addCompileCommands(
	std::vector<std::string> words, const std::string& directory, std::vector<CompileCommand>& commands)
{
	std::size_t start = 0;
	while (start < words.size() &&
		   (isAssignment(words[start]) || words[start] == "exec" || words[start] == "command"))
	{
		++start;
	}
	while (start < words.size() && isWrapper(words[start]))
	{
		++start;
		// Its own options and assignments, and their values (env FOO=1, xcrun -sdk macosx, nice -n 5).
		while (start < words.size() && !isCompiler(words[start]) &&
			   (words[start].starts_with('-') || isAssignment(words[start]) ||
				words[start - 1] == "-sdk" || words[start - 1] == "--sdk" || words[start - 1] == "-n"))
		{
			++start;
		}
	}
	if (start >= words.size() || !isCompiler(words[start]))
	{
		return;
	}
	words.erase(words.begin(), words.begin() + static_cast<std::ptrdiff_t>(start));

	const std::string compiler{lower(basename(words.front()))};
	const bool msvc = compiler == "cl" || compiler == "cl.exe" || compiler.starts_with("clang-cl");
	// The words with the shell's wildcards expanded (a recipe's $(CXX) -o app src/*.cpp).
	std::vector<std::string> expanded{words.front()};
	for (std::size_t i = 1; i < words.size(); ++i)
	{
		if (!words[i].starts_with('-') && isSourceFile(words[i]))
		{
			std::ranges::copy(expandGlob(directory, words[i]), std::back_inserter(expanded));
		}
		else
		{
			expanded.push_back(words[i]);
		}
	}
	words = std::move(expanded);

	std::vector<std::size_t> sources;
	for (std::size_t i = 1; i < words.size(); ++i)
	{
		const std::string& word = words[i];
		if (word == "-E" || word == "-M" || word == "-MM" || (msvc && (word == "/E" || word == "/P")))
		{
			return;	   // preprocesses, or lists dependencies
		}
		else if (takesValue(word))
		{
			++i;
		}
		else if (!word.starts_with('-') && !(msvc && word.starts_with('/') && !isSourceFile(word)) &&
				 isSourceFile(word))
		{
			sources.push_back(i);
		}
	}
	if (sources.empty())
	{
		return;	   // links, or asks the compiler something (--version)
	}

	for (const std::size_t source: sources)
	{
		CompileCommand command{.directory = directory, .file = words[source]};
		for (std::size_t i = 0; i < words.size(); ++i)
		{
			if (i == source || !std::ranges::contains(sources, i))
			{
				command.arguments.push_back(words[i]);
			}
		}
		commands.push_back(std::move(command));
	}
}

// The directory of `-C dir`, `-Cdir` or `--directory=dir` in a make command, if it has one.
std::optional<std::string> makeDirectoryArgument(const std::vector<std::string>& words)
{
	for (std::size_t i = 1; i < words.size(); ++i)
	{
		if (words[i] == "-C" && i + 1 < words.size())
		{
			return words[i + 1];
		}
		if (words[i].starts_with("-C") && words[i].size() > 2)
		{
			return words[i].substr(2);
		}
		if (words[i].starts_with("--directory="))
		{
			return words[i].substr(12);
		}
	}
	return std::nullopt;
}

// "make: Entering directory `/x'" (make 3.81) or "make[2]: Entering directory '/x'" (4.x).
std::optional<std::pair<bool, std::string>> directoryChange(std::string_view line)
{
	static const std::regex change{R"(^\S*make(?:\[\d+\])?: (Entering|Leaving) directory [`'"](.*)['"]\s*$)"};
	std::match_results<std::string_view::const_iterator> match;
	if (!std::regex_match(line.begin(), line.end(), match, change))
	{
		return std::nullopt;
	}
	return std::pair{match[1] == "Entering", match[2].str()};
}

// The last lines of `output` that report a make error, for a person.
std::string makeErrors(std::string_view output)
{
	std::vector<std::string_view> lines;
	for (auto&& range: std::views::split(output, '\n'))
	{
		const std::string_view line{range.begin(), range.end()};
		if (line.contains("***") || line.contains("No rule to make target") ||
			line.contains("No targets specified") || line.contains("No such file or directory") ||
			line.contains("command not found"))
		{
			lines.push_back(line);
		}
	}
	std::string text;
	for (const std::string_view line: lines | std::views::drop(lines.size() > 5 ? lines.size() - 5 : 0))
	{
		text += std::string(line) + "\n";
	}
	if (!text.empty())
	{
		text.pop_back();
	}
	return text;
}

std::string jsonString(const std::string& text)
{
	std::string json = "\"";
	for (const char c: text)
	{
		switch (c)
		{
		case '"':
			json += "\\\"";
			break;
		case '\\':
			json += "\\\\";
			break;
		case '\n':
			json += "\\n";
			break;
		case '\r':
			json += "\\r";
			break;
		case '\t':
			json += "\\t";
			break;
		default:
			if (static_cast<unsigned char>(c) < 0x20)
			{
				json += std::format("\\u{:04x}", static_cast<int>(c));
			}
			else
			{
				json += c;
			}
		}
	}
	return json + "\"";
}

Output run(
	const Runner& runner,
	const std::string& program,
	const std::vector<std::string>& arguments,
	const std::string& directory)
{
	if (runner)
	{
		return runner(program, arguments, directory);
	}
	// Waits while make prints: a large project's dry run takes longer than the default timeout.
	const utility::ProcessOutput output = utility::executeProcess(
		program, arguments, FilePath(directory), true, utility::DEFAULT_TIMEOUT);
	return {
		.output = output.output,
		.errors = output.error,
		.exitCode = output.exitCode,
		.started = !(output.exitCode == -1 && output.output.empty() && !output.error.empty())};
}

std::string commandLine(const std::string& program, const std::vector<std::string>& arguments)
{
	std::string line = program;
	for (const std::string& argument: arguments)
	{
		line += " " + (argument.find_first_of(" \t\"'") == std::string::npos ? argument : jsonString(argument));
	}
	return line;
}

std::expected<void, Error> checkInvocation(const Invocation& invocation)
{
	std::error_code error;
	if (invocation.directory.empty() || !fs::is_directory(invocation.directory, error))
	{
		return std::unexpected(Error{
			Error::Code::DirectoryMissing,
			invocation.directory.empty()
				? "The Makefile directory is not set."
				: std::format("The Makefile directory \"{}\" does not exist.", invocation.directory)});
	}
	// A -f/--file argument names the makefile itself.
	const bool namesMakefile = std::ranges::any_of(invocation.arguments, [](const std::string& argument) {
		return argument == "-f" || argument.starts_with("--file") || argument.starts_with("--makefile") ||
			(argument.starts_with("-f") && argument.size() > 2);
	});
	if (!namesMakefile && findMakefile(invocation.directory).empty())
	{
		return std::unexpected(Error{
			Error::Code::NoMakefile,
			std::format("\"{}\" has no GNUmakefile, makefile or Makefile.", invocation.directory)});
	}
	return {};
}
}	 // namespace

std::string describe(const Error& error)
{
	return error.message.empty() ? std::string(to_std_sv(error.code)) : error.message;
}

std::vector<std::string> splitArguments(std::string_view text)
{
	std::vector<std::string> arguments;
	for (Word& word: shellWords(text))
	{
		arguments.push_back(std::move(word.text));
	}
	return arguments;
}

std::string makeProgram(const std::string& program)
{
	if (!program.empty())
	{
		return program;
	}
	if constexpr (utility::Platform::isWindows())
	{
		bool found = false;
		utility::searchPath("make", found);
		return found ? "make" : "mingw32-make";
	}
	return "make";
}

std::string findMakefile(const std::string& directory)
{
	// By the names in the directory: on a case-insensitive file system makefile is Makefile too.
	std::set<std::string> names;
	std::error_code error;
	for (fs::directory_iterator it{directory, error}, end; !error && it != end; it.increment(error))
	{
		if (std::error_code fileError; it->is_regular_file(fileError))
		{
			names.insert(it->path().filename().string());
		}
	}
	for (const char* name: {"GNUmakefile", "makefile", "Makefile"})
	{
		if (names.contains(name))
		{
			return name;
		}
	}
	return {};
}

std::vector<CompileCommand> parseDryRun(std::string_view output, const std::string& directory)
{
	std::vector<CompileCommand> commands;
	// The directories make entered, the one it started in first; and the one a make it runs with
	// -C works in when it says nothing of entering it (--no-print-directory).
	std::vector<std::string> entered{withoutTrailingSlash(fs::path(directory).lexically_normal().generic_string())};
	std::optional<std::string> recursed;

	std::string pending;	// a command line continued with a backslash
	const auto handle = [&](const std::string& line) {
		if (const auto change = directoryChange(line))
		{
			recursed.reset();
			if (change->first)
			{
				entered.push_back(withoutTrailingSlash(fs::path(change->second).lexically_normal().generic_string()));
			}
			else if (entered.size() > 1)
			{
				entered.pop_back();
			}
			return;
		}

		fs::path current{recursed.value_or(entered.back())};
		std::vector<std::string> words;
		const auto endCommand = [&]() {
			if (words.empty())
			{
				return;
			}
			if (words.front() == "cd" && words.size() >= 2)
			{
				current = joined(current, words[1]);
			}
			else if (isMake(words.front()))
			{
				// A make it runs: in its -C directory, or here.
				const std::optional<std::string> sub{makeDirectoryArgument(words)};
				recursed = sub ? joined(entered.back(), *sub) : entered.back();
			}
			else
			{
				addCompileCommands(words, current.generic_string(), commands);
			}
			words.clear();
		};
		for (Word& word: shellWords(line))
		{
			if (word.separator)
			{
				endCommand();
			}
			else
			{
				words.push_back(std::move(word.text));
			}
		}
		endCommand();
	};

	for (auto&& range: std::views::split(output, '\n'))
	{
		std::string line{range.begin(), range.end()};
		if (line.ends_with('\r'))
		{
			line.pop_back();
		}
		if (line.ends_with('\\'))
		{
			line.pop_back();
			pending += line + " ";
			continue;
		}
		handle(pending + line);
		pending.clear();
	}
	if (!pending.empty())
	{
		handle(pending);
	}
	return commands;
}

std::expected<DryRun, Error> runDryRun(const Invocation& invocation, const Runner& runner)
{
	if (const auto checked = checkInvocation(invocation); !checked)
	{
		return std::unexpected(checked.error());
	}
	const std::string program{makeProgram(invocation.program)};
	// -n prints the commands without running them, -B as if nothing were built, -w where each
	// make works, -k past a goal it cannot make.
	std::vector<std::string> arguments{"-n", "-B", "-w", "-k"};
	arguments.insert(arguments.end(), invocation.arguments.begin(), invocation.arguments.end());

	const Output output{run(runner, program, arguments, invocation.directory)};
	if (!output.started)
	{
		return std::unexpected(Error{
			Error::Code::MakeNotFound,
			std::format("\"{}\" could not be started: {}", program, output.errors)});
	}
	const std::string all{output.output + "\n" + output.errors};
	return DryRun{
		.commands = parseDryRun(all, invocation.directory),
		.commandLine = commandLine(program, arguments),
		.exitCode = output.exitCode,
		.errors = makeErrors(all)};
}

Error noCompileCommands(const DryRun& dryRun, const Invocation& invocation)
{
	std::string message{std::format(
		"\"{}\" in \"{}\" would compile no C or C++ file.", dryRun.commandLine, invocation.directory)};
	if (!dryRun.errors.empty())
	{
		message += "\nmake said:\n" + dryRun.errors;
	}
	message += invocation.arguments.empty()
		? "\nGive the goal that builds the project (the makefile's default goal may only print help)."
		: "\nCheck the goal and variables, or that the makefiles a generator writes exist.";
	return {Error::Code::NoCompileCommands, std::move(message)};
}

std::vector<CompileCommand> firstPerFile(const std::vector<CompileCommand>& commands)
{
	std::vector<CompileCommand> first;
	std::set<std::string> files;
	for (const CompileCommand& command: commands)
	{
		if (files.insert(joined(command.directory, command.file)).second)
		{
			first.push_back(command);
		}
	}
	return first;
}

std::string toCompilationDatabase(const std::vector<CompileCommand>& commands)
{
	std::string json = "[\n";
	for (const CompileCommand& command: commands)
	{
		json += "  {\n    \"directory\": " + jsonString(command.directory) + ",\n    \"file\": " +
			jsonString(command.file) + ",\n    \"arguments\": [";
		for (std::size_t i = 0; i < command.arguments.size(); ++i)
		{
			json += (i == 0 ? "" : ", ") + jsonString(command.arguments[i]);
		}
		json += "]\n  }";
		json += &command == &commands.back() ? "\n" : ",\n";
	}
	return json + "]\n";
}

Targets parseTargets(std::string_view database)
{
	Targets targets;
	// A rule line: "name: prerequisites", or "name1 name2: ..."; not a variable (name := value).
	static const std::regex rule{R"(^([^\s#:=%$][^#:=%$]*):(?!=)(?:[^=]|$).*)"};
	bool notATarget = false;
	bool inFiles = false;
	for (auto&& range: std::views::split(database, '\n'))
	{
		const std::string line{range.begin(), range.end()};
		if (line.starts_with(".DEFAULT_GOAL := "))
		{
			targets.defaultGoal = line.substr(17);
			continue;
		}
		if (line.starts_with("# Files"))
		{
			inFiles = true;
			continue;
		}
		if (!inFiles)
		{
			continue;
		}
		if (line == "# Not a target:")
		{
			notATarget = true;
			continue;
		}
		std::smatch match;
		if (!line.starts_with('\t') && std::regex_match(line, match, rule))
		{
			if (!notATarget)
			{
				for (const std::string& name: splitArguments(match[1].str()))
				{
					if (!name.starts_with('.') && !name.contains('/') && !name.contains('\\') &&
						!std::ranges::contains(targets.targets, name))
					{
						targets.targets.push_back(name);
					}
				}
			}
			notATarget = false;
		}
		else if (!line.starts_with('#'))
		{
			notATarget = false;
		}
	}
	return targets;
}

Targets listTargets(const Invocation& invocation, const Runner& runner)
{
	if (!checkInvocation(invocation))
	{
		return {};
	}
	// The database (-p) without the built-in rules and variables (-r -R), running nothing: -q asks
	// whether a goal is up to date, of a goal no makefile has (make prints the database anyway).
	std::vector<std::string> arguments{"-p", "-r", "-R", "-q", "-n"};
	for (const std::string& argument: invocation.arguments)
	{
		if (isAssignment(argument) || argument.starts_with('-'))
		{
			arguments.push_back(argument);	  // variables and options pick the makefile's branches
		}
	}
	arguments.emplace_back("__sourcetrail_list_targets__");
	const Output output{run(runner, makeProgram(invocation.program), arguments, invocation.directory)};
	return parseTargets(output.output + "\n" + output.errors);
}

std::vector<std::string> rankTargets(const Targets& targets)
{
	using utility::Platform;
	const std::array<std::string_view, 5> mac{"osx", "macos", "darwin", "mac", "apple"};
	const std::array<std::string_view, 1> linux{"linux"};
	const std::array<std::string_view, 6> windows{"windows", "win", "win32", "win64", "mingw", "msvc"};
	const auto hostOs = Platform::isMac() ? std::span<const std::string_view>(mac)
		: Platform::isLinux()			  ? std::span<const std::string_view>(linux)
										  : std::span<const std::string_view>(windows);
#if defined(__aarch64__) || defined(_M_ARM64)
	const std::array<std::string_view, 3> hostArch{"arm64", "aarch64", "arm64e"};
	const std::array<std::string_view, 4> otherArch{"x64", "x86_64", "amd64", "x86"};
#else
	const std::array<std::string_view, 3> hostArch{"x64", "x86_64", "amd64"};
	const std::array<std::string_view, 4> otherArch{"arm64", "aarch64", "arm", "x86"};
#endif
	const std::array<std::string_view, 15> excluded{
		"clean", "distclean", "mrproper", "install", "uninstall", "help", "test", "tests", "check",
		"dist", "docs", "doc", "format", "lint", "release-notes"};

	// osx-arm64-debug: osx, arm64, debug.
	const auto words = [](const std::string& target) {
		std::vector<std::string> parts{""};
		for (const char c: lower(target))
		{
			if (c == '-' || c == '_' || c == '.')
			{
				parts.emplace_back();
			}
			else
			{
				parts.back() += c;
			}
		}
		return parts;
	};
	const auto has = [](const std::vector<std::string>& parts, std::span<const std::string_view> names) {
		return std::ranges::any_of(parts, [&](const std::string& part) { return std::ranges::contains(names, part); });
	};
	const auto otherOs = [&](const std::vector<std::string>& parts) {
		return (!Platform::isMac() && has(parts, mac)) || (!Platform::isLinux() && has(parts, linux)) ||
			(!Platform::isWindows() && has(parts, windows)) ||
			std::ranges::any_of(parts, [](const std::string& part) {
				return part == "android" || part == "ios" || part == "wasm" || part == "rpi" || part == "emscripten";
			});
	};

	std::vector<std::string> ranked;
	const auto add = [&](const std::string& target) {
		if (!target.empty() && !std::ranges::contains(ranked, target) &&
			!std::ranges::contains(excluded, lower(target)))
		{
			ranked.push_back(target);
		}
	};
	add(targets.defaultGoal);
	if (std::ranges::contains(targets.targets, "all"))
	{
		add("all");
	}

	std::vector<std::pair<int, std::string>> scored;
	for (const std::string& target: targets.targets)
	{
		const std::vector<std::string> parts{words(target)};
		if (std::ranges::any_of(parts, [&](const std::string& part) { return std::ranges::contains(excluded, part); }) ||
			otherOs(parts))
		{
			continue;
		}
		int score = 0;
		score += has(parts, hostOs) ? 4 : 0;
		score += has(parts, hostArch) ? 2 : 0;
		score -= has(parts, otherArch) ? 2 : 0;
		score += std::ranges::contains(parts, "debug") ? 1 : 0;
		if (score > 0)
		{
			scored.emplace_back(score, target);
		}
	}
	std::ranges::stable_sort(scored, std::ranges::greater{}, &std::pair<int, std::string>::first);
	for (const auto& [score, target]: scored)
	{
		add(target);
	}
	return ranked;
}

std::optional<Suggestion> suggestTarget(const Invocation& invocation, std::size_t tries, const Runner& runner)
{
	const Targets targets{listTargets(invocation, runner)};
	std::vector<std::string> goals{rankTargets(targets)};
	if (targets.defaultGoal.empty())
	{
		goals.insert(goals.begin(), std::string{});	// make's own choice, which its database did not say
	}
	for (const std::string& target: goals | std::views::take(tries))
	{
		Invocation goal{invocation};
		if (!target.empty())
		{
			goal.arguments.push_back(target);
		}
		if (const auto dryRun = runDryRun(goal, runner); dryRun && !dryRun->commands.empty())
		{
			return Suggestion{.target = target, .dryRun = *dryRun};
		}
	}
	return std::nullopt;
}
}	 // namespace make
