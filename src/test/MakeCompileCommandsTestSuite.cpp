#include "Catch2.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "MakeCompileCommands.h"

namespace
{
using Strings = std::vector<std::string>;

// A directory of the test's own, removed when it ends.
struct TemporaryDirectory
{
	TemporaryDirectory()
		: path(std::filesystem::temp_directory_path() /
			   ("sourcetrail_make_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
	{
		std::filesystem::create_directories(path);
	}
	~TemporaryDirectory()
	{
		std::error_code error;
		std::filesystem::remove_all(path, error);
	}
	void write(const std::string& name, const std::string& text) const
	{
		std::ofstream(path / name) << text;
	}
	std::string str() const
	{
		return path.generic_string();
	}

	std::filesystem::path path;
};
}	 // namespace

TEST_CASE("make dry run: the compile commands of a GENie makefile, as make 3.81 prints them")
{
	// bgfx: the top-level makefile runs make in the generated project, which runs one make per
	// project with --no-print-directory -C . (so says nothing of entering it).
	const std::string output = R"(make: Entering directory `/src/bgfx'
../bx/tools/bin/darwin/genie --gcc=osx-arm64 gmake
/usr/bin/make -R -C .build/projects/gmake-osx-arm64 config=debug
make[1]: Entering directory `/src/bgfx/.build/projects/gmake-osx-arm64'
echo "==== Building bx (debug) ===="
/usr/bin/make --no-print-directory -C . -f bx.make
echo Creating ../../osx-arm64/obj/Debug/bx
mkdir -p "../../osx-arm64/obj/Debug/bx"
echo allocator.cpp
clang++    -MMD -MP -D_DEBUG -I"../../../../bx/include" -std=c++20 -arch arm64  -o "../../osx-arm64/obj/Debug/bx/allocator.o" -MF "../../osx-arm64/obj/Debug/bx/allocator.d" -c "../../../../bx/src/allocator.cpp"
ar -rcs ../../osx-arm64/bin/libbxDebug.a ../../osx-arm64/obj/Debug/bx/allocator.o
clang++ -o ../../osx-arm64/bin/toolDebug ../../osx-arm64/obj/Debug/tool/main.o -L"." -lbxDebug
make[1]: Leaving directory `/src/bgfx/.build/projects/gmake-osx-arm64'
make: Leaving directory `/src/bgfx'
)";
	const std::vector<make::CompileCommand> commands = make::parseDryRun(output, "/src/bgfx");

	REQUIRE(commands.size() == 1);
	REQUIRE(commands[0].directory == "/src/bgfx/.build/projects/gmake-osx-arm64");
	REQUIRE(commands[0].file == "../../../../bx/src/allocator.cpp");
	REQUIRE(
		commands[0].arguments ==
		Strings {
			"clang++", "-MMD", "-MP", "-D_DEBUG", "-I../../../../bx/include", "-std=c++20", "-arch", "arm64", "-o",
			"../../osx-arm64/obj/Debug/bx/allocator.o", "-MF", "../../osx-arm64/obj/Debug/bx/allocator.d", "-c",
			"../../../../bx/src/allocator.cpp"});
}

TEST_CASE("make dry run: make 4 quoting, cd, wrappers, continued lines and several files per command")
{
	const std::string output = R"(make[1]: Entering directory '/work/lib'
cd sub && ccache gcc -DNAME='"lib"' -c a.c b.c
CC=gcc env FOO=1 aarch64-linux-gnu-g++-13 -Iinclude \
	-c src/x.cpp -o x.o
echo gcc -c fake.c
gcc -E pre.c
make[1]: Leaving directory '/work/lib'
)";
	const std::vector<make::CompileCommand> commands = make::parseDryRun(output, "/work");

	REQUIRE(commands.size() == 3);
	REQUIRE(commands[0].directory == "/work/lib/sub");
	REQUIRE(commands[0].file == "a.c");
	REQUIRE(commands[0].arguments == Strings {"gcc", "-DNAME=\"lib\"", "-c", "a.c"});
	REQUIRE(commands[1].file == "b.c");
	REQUIRE(commands[1].arguments == Strings {"gcc", "-DNAME=\"lib\"", "-c", "b.c"});
	REQUIRE(commands[2].directory == "/work/lib");
	REQUIRE(commands[2].file == "src/x.cpp");
	REQUIRE(commands[2].arguments == Strings {"aarch64-linux-gnu-g++-13", "-Iinclude", "-c", "src/x.cpp", "-o", "x.o"});
}

TEST_CASE("make dry run: a make run with -C works there until another starts")
{
	const std::string output = R"(make -C liba --no-print-directory
cc -c a.c
make -C libb --no-print-directory
cc -c b.c
)";
	const std::vector<make::CompileCommand> commands = make::parseDryRun(output, "/p");

	REQUIRE(commands.size() == 2);
	REQUIRE(commands[0].directory == "/p/liba");
	REQUIRE(commands[1].directory == "/p/libb");
}

TEST_CASE("make arguments split as the shell splits them")
{
	REQUIRE(make::splitArguments(R"(osx-arm64-debug config=debug "CFLAGS=-O0 -g" a\ b)") ==
		Strings {"osx-arm64-debug", "config=debug", "CFLAGS=-O0 -g", "a b"});
	REQUIRE(make::splitArguments("  ").empty());
}

TEST_CASE("make targets: a makefile's goals from its database, not its files, variables or specials")
{
	const std::string database = R"(# Variables
.DEFAULT_GOAL := help
MAKEFILE_LIST :=  makefile
CC = cc
# Files

# Not a target:
makefile:
#  Last modified 2026-11-01 12:00:00

help:
#  Phony target (prerequisite of .PHONY).
	@echo Available targets:

.PHONY: help all clean

all: lib tool

lib tool: src/a.o

.build/projects/gmake-osx-arm64:
	genie gmake

%.o: %.c

osx-arm64-debug: .build/projects/gmake-osx-arm64 ## Build - macOS ARM64 Debug
	$(MAKE) -C .build/projects/gmake-osx-arm64 config=debug
)";
	const make::Targets targets = make::parseTargets(database);

	REQUIRE(targets.defaultGoal == "help");
	REQUIRE(targets.targets == Strings {"help", "all", "lib", "tool", "osx-arm64-debug"});
}

TEST_CASE("make targets: the default goal first, then all, never one that cleans or prints help")
{
	const make::Targets targets {.targets = {"clean", "help", "install", "all", "build"}, .defaultGoal = "build"};
	REQUIRE(make::rankTargets(targets) == Strings {"build", "all"});

	const make::Targets helpFirst {.targets = {"help", "all", "clean"}, .defaultGoal = "help"};
	REQUIRE(make::rankTargets(helpFirst) == Strings {"all"});
}

TEST_CASE("make targets: this platform's debug build before its release build and other platforms")
{
#if defined(__APPLE__)
	const std::string os = "osx";
#elif defined(_WIN32)
	const std::string os = "mingw";
#else
	const std::string os = "linux";
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
	const std::string arch = "arm64";
#else
	const std::string arch = "x64";
#endif
	const make::Targets targets {
		.targets = {"android-arm-debug", os + "-" + arch + "-release", os + "-" + arch + "-debug", "wasm-debug", "help"},
		.defaultGoal = "help"};
	const Strings ranked = make::rankTargets(targets);

	REQUIRE(ranked.size() == 2);
	REQUIRE(ranked[0] == os + "-" + arch + "-debug");
	REQUIRE(ranked[1] == os + "-" + arch + "-release");
}

TEST_CASE("make dry run: what runs, and why it cannot")
{
	TemporaryDirectory directory;
	Strings ran;
	const make::Runner runner = [&](const std::string& program, const Strings& arguments, const std::string& in) {
		ran = arguments;
		ran.insert(ran.begin(), program);
		REQUIRE(in == directory.str());
		return make::Output {.output = "cc -c main.c\n", .exitCode = 0};
	};

	const auto missing = make::runDryRun({.directory = directory.str() + "/nowhere"}, runner);
	REQUIRE_FALSE(missing);
	REQUIRE(missing.error().code == make::Error::Code::DirectoryMissing);

	const auto noMakefile = make::runDryRun({.directory = directory.str()}, runner);
	REQUIRE_FALSE(noMakefile);
	REQUIRE(noMakefile.error().code == make::Error::Code::NoMakefile);

	directory.write("Makefile", "all:\n\tcc -c main.c\n");
	const auto dryRun = make::runDryRun({.directory = directory.str(), .program = "gmake", .arguments = {"all"}}, runner);
	REQUIRE(dryRun);
	REQUIRE(ran == Strings {"gmake", "-n", "-B", "-w", "-k", "all"});
	REQUIRE(dryRun->commands.size() == 1);
	REQUIRE(dryRun->commands[0].directory == directory.str());

	const auto notStarted = make::runDryRun({.directory = directory.str()}, [](auto&&...) {
		return make::Output {.errors = "No such file or directory", .exitCode = -1, .started = false};
	});
	REQUIRE_FALSE(notStarted);
	REQUIRE(notStarted.error().code == make::Error::Code::MakeNotFound);
}

TEST_CASE("make targets: the suggestion is the first goal whose dry run compiles")
{
	TemporaryDirectory directory;
	directory.write("Makefile", "help:\n");
	const make::Runner runner = [](const std::string&, const Strings& arguments, const std::string&) {
		if (std::ranges::contains(arguments, "-p"))
		{
			return make::Output {.output = ".DEFAULT_GOAL := help\n# Files\nhelp:\nall:\n"};
		}
		if (arguments.back() == "all")
		{
			return make::Output {.output = "gcc -c main.c\n"};
		}
		return make::Output {.output = "echo Available targets:\n"};
	};

	const auto suggestion = make::suggestTarget({.directory = directory.str()}, 6, runner);
	REQUIRE(suggestion);
	REQUIRE(suggestion->target == "all");
	REQUIRE(suggestion->dryRun.commands.size() == 1);
}

TEST_CASE("make compile commands: a file compiled twice keeps its first command")
{
	const std::vector<make::CompileCommand> commands = make::firstPerFile({
		{.directory = "/p/lib", .file = "../src/a.c", .arguments = {"cc", "-c", "../src/a.c"}},
		{.directory = "/p", .file = "src/b.c", .arguments = {"cc", "-c", "src/b.c"}},
		{.directory = "/p/shared", .file = "/p/src/a.c", .arguments = {"cc", "-DSHARED", "-c", "/p/src/a.c"}},
	});
	REQUIRE(commands.size() == 2);
	REQUIRE(commands[0].arguments == Strings {"cc", "-c", "../src/a.c"});
	REQUIRE(commands[1].file == "src/b.c");
}

TEST_CASE("make compile commands as a compilation database")
{
	const std::string json = make::toCompilationDatabase(
		{{.directory = "/p", .file = "a \"b\".c", .arguments = {"cc", "-DX=\"1\"", "-c", "a \"b\".c"}}});
	REQUIRE(json.contains(R"("directory": "/p")"));
	REQUIRE(json.contains(R"("file": "a \"b\".c")"));
	REQUIRE(json.contains(R"("arguments": ["cc", "-DX=\"1\"", "-c", "a \"b\".c"])"));
}
