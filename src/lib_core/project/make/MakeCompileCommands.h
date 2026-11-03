#ifndef MAKE_COMPILE_COMMANDS_H
#define MAKE_COMPILE_COMMANDS_H

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The compile commands of a Makefile project, read from what make prints for a dry run
// (make -n -B -w -k): every command it would run to build the goals from scratch, without
// running any (only recursive makes run, as dry runs themselves). The ones that compile C,
// C++ or Objective-C become a compilation database, so indexing reads them as it reads a
// compile_commands.json.
//
// What a dry run cannot show: commands a recipe builds at run time (a shell loop, a script
// that calls the compiler), and makefiles a recipe would generate first (a project generator
// run from the makefile): run the generator once, or point the group at what it generated.
namespace make
{
// How make is run: in `directory` (an absolute path), as `program` (empty: make), with
// `arguments` (goals and variables, as a person types them after make).
struct Invocation
{
	std::string directory;
	std::string program;
	std::vector<std::string> arguments;
};

// One file a command compiles. `arguments` is the whole command, compiler first, with this file
// and no other the command compiles.
struct CompileCommand
{
	std::string directory;
	std::string file;
	std::vector<std::string> arguments;
};

struct DryRun
{
	std::vector<CompileCommand> commands;
	std::string commandLine;	// what ran, for a person
	int exitCode = 0;
	std::string errors;	   // what make wrote to stderr, the last lines
};

struct Error
{
	enum class Code : std::uint8_t
	{
		DirectoryMissing,	 // the directory is empty or does not exist
		NoMakefile,			 // no GNUmakefile, makefile or Makefile in it
		MakeNotFound,		 // the make program could not be started
		NoCompileCommands,	 // it ran, and would compile nothing
	};
	Code code{};
	std::string message;
};

constexpr std::string_view to_std_sv(Error::Code code) noexcept
{
	using enum Error::Code;
	switch (code)
	{
	case DirectoryMissing:
		return "the Makefile directory is not set or does not exist";
	case NoMakefile:
		return "the directory has no Makefile";
	case MakeNotFound:
		return "make could not be started";
	case NoCompileCommands:
		return "make's dry run compiles no file";
	}
	return "unknown error";
}

// The message of `error`, or its code's fallback when it has none.
std::string describe(const Error& error);

// What running `program` with `arguments` in `directory` printed: stdout, stderr and the exit
// code. Tests give their own; empty: the program runs.
struct Output
{
	std::string output;
	std::string errors;
	int exitCode = 0;
	bool started = true;
};
using Runner = std::function<Output(
	const std::string& program, const std::vector<std::string>& arguments, const std::string& directory)>;

// Words as a POSIX shell splits them: quotes and backslashes group, and are removed.
std::vector<std::string> splitArguments(std::string_view text);

// The program make runs as: `program`, or make (mingw32-make on Windows when make is missing).
std::string makeProgram(const std::string& program);

// The makefile make reads in `directory` (GNUmakefile, makefile, Makefile); empty for none.
std::string findMakefile(const std::string& directory);

// The compile commands in the output of a dry run started in `directory`.
std::vector<CompileCommand> parseDryRun(std::string_view output, const std::string& directory);

// Runs the dry run.
[[nodiscard]] std::expected<DryRun, Error> runDryRun(const Invocation& invocation, const Runner& runner = {});

// Why `dryRun` compiles nothing, for a person: what ran, what make said, what to try.
Error noCompileCommands(const DryRun& dryRun, const Invocation& invocation);

// The first command of each file: a file a makefile compiles twice (a static and a shared library
// of the same sources) is indexed once.
std::vector<CompileCommand> firstPerFile(const std::vector<CompileCommand>& commands);

// `commands` as a compile_commands.json.
std::string toCompilationDatabase(const std::vector<CompileCommand>& commands);

// The goals a makefile has: what make prints of its database (make -p), its own targets, not
// special (.PHONY), pattern (%.o) or file (build/x.o) ones, in the order it lists them; and its
// default goal.
struct Targets
{
	std::vector<std::string> targets;
	std::string defaultGoal;
};
Targets parseTargets(std::string_view database);
Targets listTargets(const Invocation& invocation, const Runner& runner = {});

// The goals worth a dry run, likeliest to compile the project first: the default goal, all,
// then those named for this platform (osx-arm64-debug on an arm64 Mac), a debug build before
// a release one. Goals that clean, install, test or print help are left out.
std::vector<std::string> rankTargets(const Targets& targets);

// The first of `rankTargets` (at most `tries`) whose dry run compiles anything, with that run;
// none when no goal does. An empty target is make's default goal, tried first when the
// database does not name it.
struct Suggestion
{
	std::string target;
	DryRun dryRun;
};
std::optional<Suggestion> suggestTarget(
	const Invocation& invocation, std::size_t tries = 6, const Runner& runner = {});
}	 // namespace make

#endif	  // MAKE_COMPILE_COMMANDS_H
