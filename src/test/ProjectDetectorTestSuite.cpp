#include "Catch2.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ProjectDetector.h"
#include "TestTempDirectory.h"
#include "ToolChain.h"

namespace
{
namespace fs = std::filesystem;

using Strings = std::vector<std::string>;

// `path` with `content`, its directories created.
void write(const fs::path& path, const std::string& content = {})
{
	fs::create_directories(path.parent_path());
	std::ofstream{path, std::ios::binary} << content;
}

// Every program is on PATH but those named.
ProjectDetectorOptions without(Strings missing = {}, std::string location = {})
{
	return {
		.projectLocation = std::move(location),
		.hasExecutable = [missing = std::move(missing)](const std::string& name) {
			return !std::ranges::contains(missing, name);
		}};
}

ProjectProposal detect(const fs::path& folder, const ProjectDetectorOptions& options = without())
{
	const auto proposal{detectProject(folder.string(), options)};
	REQUIRE(proposal.has_value());
	return *proposal;
}

std::string text(const ProjectProposal& proposal, const std::string& field)
{
	REQUIRE(proposal.fields.contains(field));
	return std::get<std::string>(proposal.fields.at(field));
}

Strings strings(const ProjectProposal& proposal, const std::string& field)
{
	REQUIRE(proposal.fields.contains(field));
	return std::get<Strings>(proposal.fields.at(field));
}

bool mentions(const Strings& lines, const std::string& part)
{
	return std::ranges::any_of(lines, [&](const std::string& line) { return line.contains(part); });
}

std::string generic(const fs::path& path)
{
	return path.generic_string();
}

// A CMakeCache.txt of a build directory CMake configured for `source`.
void writeCache(const fs::path& buildDir, const fs::path& source, const std::string& generator = "Ninja")
{
	write(
		buildDir / "CMakeCache.txt",
		"CMAKE_GENERATOR:INTERNAL=" + generator + "\nCMAKE_HOME_DIRECTORY:INTERNAL=" + generic(source) + "\n");
}
}	 // namespace

TEST_CASE("ProjectDetector refuses a folder that does not exist, and a file")
{
	const TestTempDirectory temp{"detectMissing"};
	write(temp.getPath() / "file.txt");

	const auto missing{detectProject(generic(temp.getPath() / "nope"))};
	REQUIRE(!missing.has_value());
	CHECK(missing.error() == ProjectDetectionError::FolderMissing);

	const auto file{detectProject(generic(temp.getPath() / "file.txt"))};
	REQUIRE(!file.has_value());
	CHECK(file.error() == ProjectDetectionError::NotAFolder);
}

TEST_CASE("ProjectDetector names the project after the folder, as a valid project name")
{
	const TestTempDirectory temp{"detectName"};
	const fs::path folder{temp.getPath() / "my lib (v2)"};
	write(folder / "main.c", "int main(void) { return 0; }\n");

	const ProjectProposal proposal{detect(folder)};

	CHECK(proposal.projectName == "my_lib__v2_");
	CHECK(proposal.projectLocation == generic(folder));
	CHECK(proposal.folder == generic(folder));
}

TEST_CASE("ProjectDetector uses the first usable configure preset")
{
	const TestTempDirectory temp{"detectPresets"};
	const fs::path folder{temp.getPath() / "app"};
	write(folder / "CMakeLists.txt", "project(app)\n");
	write(folder / "CMakePresets.json", R"({"version": 6, "configurePresets": [
		{"name": "base", "hidden": true, "binaryDir": "${sourceDir}/build/${presetName}"},
		{"name": "vcpkg", "inherits": "base",
		 "toolchainFile": "$env{SOURCETRAIL_TEST_SURELY_UNSET_ROOT}/scripts/buildsystems/vcpkg.cmake"},
		{"name": "fast", "inherits": "base", "generator": "Ninja"},
		{"name": "nobinary", "generator": "Unix Makefiles"},
		{"name": "default", "inherits": "base", "displayName": "Default", "generator": "Unix Makefiles"},
		{"name": "release", "inherits": "base", "generator": "Unix Makefiles"}
	]})");

	const ProjectProposal proposal{detect(folder, without({"ninja"}))};

	CHECK(proposal.sourceGroupType == SourceGroupType::CXX_CMAKE_FILE_API);
	CHECK(text(proposal, "cmake_preset") == "default");
	CHECK(text(proposal, "cmake_source_directory") == generic(folder));
	CHECK(strings(proposal, "indexed_header_paths") == Strings{generic(folder)});
	CHECK(proposal.reasons.front().contains("Found CMakePresets.json: using configure preset 'default'"));
	CHECK(proposal.reasons.front().contains("(Unix Makefiles)"));
	CHECK(mentions(proposal.reasons, "'vcpkg': it reads $env{SOURCETRAIL_TEST_SURELY_UNSET_ROOT}"));
	CHECK(mentions(proposal.reasons, "'fast': its generator is Ninja, and ninja is not on PATH"));
	CHECK(mentions(proposal.reasons, "'nobinary': it has no binaryDir"));
	CHECK(mentions(proposal.reasons, "Other presets to choose from: 'release'"));
}

TEST_CASE("ProjectDetector prefers a configure preset whose build directory is configured")
{
	const TestTempDirectory temp{"detectConfiguredPreset"};
	const fs::path folder{temp.getPath() / "app"};
	write(folder / "CMakeLists.txt", "project(app)\n");
	write(folder / "CMakePresets.json", R"({"version": 3, "configurePresets": [
		{"name": "debug", "binaryDir": "${sourceDir}/out/debug"},
		{"name": "release", "binaryDir": "${sourceDir}/out/release"}
	]})");
	writeCache(folder / "out" / "release", folder);

	const ProjectProposal proposal{detect(folder)};

	CHECK(text(proposal, "cmake_preset") == "release");
	CHECK(mentions(proposal.reasons, "(out/release) is configured by CMake already: Sourcetrail asks CMake for its targets"));
}

TEST_CASE("ProjectDetector prefers the configured preset built last")
{
	const TestTempDirectory temp{"detectBuiltLastPreset"};
	const fs::path folder{temp.getPath() / "app"};
	write(folder / "CMakeLists.txt", "project(app)\n");
	write(folder / "CMakePresets.json", R"({"version": 3, "configurePresets": [
		{"name": "old", "binaryDir": "${sourceDir}/out/old"},
		{"name": "current", "binaryDir": "${sourceDir}/out/current"}
	]})");
	// Both configured, "old" even after "current"; "current" was built since.
	const auto now{fs::file_time_type::clock::now()};
	for (const char* preset: {"old", "current"})
	{
		const fs::path buildDir{folder / "out" / preset};
		writeCache(buildDir, folder);
		write(buildDir / ".ninja_log");
		for (const fs::path& file: {buildDir, buildDir / "CMakeCache.txt", buildDir / ".ninja_log"})
		{
			fs::last_write_time(file, now - std::chrono::hours{48});
		}
	}
	fs::last_write_time(folder / "out" / "old" / "CMakeCache.txt", now - std::chrono::hours{24});
	fs::last_write_time(folder / "out" / "current" / ".ninja_log", now - std::chrono::hours{1});

	const ProjectProposal proposal{detect(folder)};

	CHECK(text(proposal, "cmake_preset") == "current");
	CHECK(mentions(proposal.reasons, "(out/current, built last of the 2 configured) is configured by CMake already"));
}

TEST_CASE("ProjectDetector prefers the configure preset made for this computer")
{
	const TestTempDirectory temp{"detectPlatformPreset"};
	const fs::path folder{temp.getPath() / "lib"};
	write(folder / "CMakeLists.txt", "project(lib)\n");
	write(folder / "CMakePresets.json", R"({"version": 3, "configurePresets": [
		{"name": "dev", "hidden": true, "binaryDir": "${sourceDir}/build"},
		{"name": "dev-win", "inherits": "dev", "description": "Windows preset for library developers"},
		{"name": "dev-linux", "inherits": "dev", "description": "Linux preset for library developers"},
		{"name": "dev-mac", "inherits": "dev"}
	]})");

	const ProjectProposal proposal{detect(folder)};

#if defined(_WIN32)
	CHECK(text(proposal, "cmake_preset") == "dev-win");
#elif defined(__APPLE__)
	CHECK(text(proposal, "cmake_preset") == "dev-mac");
	CHECK(mentions(proposal.reasons, "'dev-win' looks made for Windows, and this is macOS"));
#else
	CHECK(text(proposal, "cmake_preset") == "dev-linux");
#endif
}

TEST_CASE("ProjectDetector uses CMake without a preset when no preset is usable")
{
	const TestTempDirectory temp{"detectNoUsablePreset"};
	const fs::path folder{temp.getPath() / "app"};
	write(folder / "CMakeLists.txt", "project(app)\n");
	write(folder / "CMakePresets.json", R"({"version": 3, "configurePresets": [
		{"name": "msvc", "binaryDir": "b", "generator": "Visual Studio 17 2022",
		 "condition": {"type": "equals", "lhs": "${hostSystemName}", "rhs": "Nowhere"}}
	]})");

	const ProjectProposal proposal{detect(folder)};

	CHECK(proposal.sourceGroupType == SourceGroupType::CXX_CMAKE_FILE_API);
	CHECK(text(proposal, "cmake_preset").empty());
	CHECK(!text(proposal, "cmake_build_directory").empty());
	CHECK(mentions(proposal.reasons, "has no configure preset for this computer"));
}

TEST_CASE("ProjectDetector uses a build directory CMake configured for the folder")
{
	const TestTempDirectory temp{"detectBuildDir"};
	const fs::path folder{temp.getPath() / "lib"};
	write(folder / "CMakeLists.txt", "project(lib)\n");
	writeCache(folder / "cmake-build-other", temp.getPath() / "elsewhere");	// another project's
	writeCache(folder / "build" / "debug", folder, "Unix Makefiles");

	const ProjectProposal proposal{detect(folder)};

	CHECK(proposal.sourceGroupType == SourceGroupType::CXX_CMAKE_FILE_API);
	CHECK(text(proposal, "cmake_preset").empty());
	CHECK(text(proposal, "cmake_build_directory") == generic(folder / "build" / "debug"));
	CHECK(proposal.reasons.front().contains("build/debug"));
	CHECK(proposal.reasons.front().contains("(Unix Makefiles)"));
}

TEST_CASE("ProjectDetector lets CMake configure a build directory beside the project file")
{
	const TestTempDirectory temp{"detectOwnBuildDir"};
	const fs::path folder{temp.getPath() / "src" / "lib"};
	const fs::path location{temp.getPath() / "projects"};
	write(folder / "CMakeLists.txt", "project(lib)\n");

	const ProjectProposal proposal{detect(folder, without({}, generic(location)))};

	CHECK(proposal.projectLocation == generic(location));
	CHECK(text(proposal, "cmake_build_directory") == generic(location / "lib.srctrl-build"));
	CHECK(proposal.reasons.front().contains("Found CMakeLists.txt without presets"));
}

TEST_CASE("ProjectDetector takes a compilation database when there is no CMakeLists.txt")
{
	const TestTempDirectory temp{"detectCdb"};
	const fs::path folder{temp.getPath() / "tool"};
	write(folder / "meson.build", "project('tool', 'c')\n");
	write(folder / "builddir" / "compile_commands.json", "[]");

	const ProjectProposal proposal{detect(folder)};

	CHECK(proposal.sourceGroupType == SourceGroupType::CXX_CDB);
	CHECK(text(proposal, "compilation_database") == generic(folder / "builddir" / "compile_commands.json"));
	CHECK(strings(proposal, "indexed_header_paths") == Strings{generic(folder)});
}

TEST_CASE("ProjectDetector takes the compilation database when cmake is missing")
{
	const TestTempDirectory temp{"detectCdbWithoutCMake"};
	const fs::path folder{temp.getPath() / "app"};
	write(folder / "CMakeLists.txt", "project(app)\n");
	write(folder / "compile_commands.json", "[]");

	const ProjectProposal proposal{detect(folder, without({"cmake"}))};

	CHECK(proposal.sourceGroupType == SourceGroupType::CXX_CDB);
	CHECK(mentions(proposal.reasons, "cmake is not on PATH"));
}

TEST_CASE("ProjectDetector proposes an empty C++ group for plain sources")
{
	const TestTempDirectory temp{"detectPlainCpp"};
	const fs::path folder{temp.getPath() / "plain"};
	write(folder / "src" / "main.cpp", "#include <optional>\nint main() { if constexpr (true) {} }\n");
	write(folder / "src" / "util.cc", "int util() { return 1; }\n");
	write(folder / "include" / "plain" / "util.hpp", "#pragma once\nint util();\n");
	write(folder / "tools" / "gen.c", "int gen(void) { return 0; }\n");
	write(folder / "build" / "generated.cpp", "int generated();\n");	// a build tree: not counted
	write(folder / "third_party" / "zlib" / "LICENSE", "zlib\n");	// a vendored project
	write(folder / "third_party" / "zlib" / "include" / "zlib.h", "int deflate(void);\n");
	write(folder / "Makefile", "all:\n\tc++ src/*.cpp\n");

	const ProjectProposal proposal{detect(folder)};

	CHECK(proposal.sourceGroupType == SourceGroupType::CXX_EMPTY);
	CHECK(strings(proposal, "source_paths") == Strings{generic(folder)});
	CHECK(strings(proposal, "extensions") == Strings{".cpp", ".cc"});
	CHECK(strings(proposal, "exclude_filters") == Strings{generic(folder / "build"), generic(folder / "third_party")});
	// The vendored library's headers are still found.
	CHECK(
		strings(proposal, "header_search_paths") ==
		Strings{generic(folder / "include"), generic(folder / "third_party" / "zlib" / "include")});
	if (std::ranges::contains(ClangCompiler::getAvailableCppStandards(), "c++17"))
	{
		CHECK(text(proposal, "cpp_standard") == "c++17");
		CHECK(mentions(proposal.reasons, "C++ standard c++17: the code uses if constexpr (src/main.cpp)"));
	}
	CHECK(mentions(proposal.reasons, "The 1 .c file is not in this C++ group"));
	CHECK(mentions(proposal.reasons, "third_party/ holds vendored dependencies: excluded"));
	CHECK(mentions(proposal.notices, "Makefile: its flags are not read"));
}

TEST_CASE("ProjectDetector indexes the headers of a header-only C++ library")
{
	const TestTempDirectory temp{"detectHeaderOnly"};
	const fs::path folder{temp.getPath() / "hdr"};
	write(folder / "include" / "hdr" / "hdr.h", "#pragma once\nnamespace hdr {\ntemplate <typename T> T id(T t) { return t; }\n}\n");

	const ProjectProposal proposal{detect(folder)};

	CHECK(proposal.sourceGroupType == SourceGroupType::CXX_EMPTY);
	CHECK(strings(proposal, "extensions") == Strings{".h"});
	CHECK(mentions(proposal.reasons, "The .h files are C++"));
	CHECK(mentions(proposal.reasons, "header-only library"));
}

TEST_CASE("ProjectDetector proposes an empty C group with the Makefile's standard and include paths")
{
	const TestTempDirectory temp{"detectPlainC"};
	const fs::path folder{temp.getPath() / "editor"};
	write(folder / "editor.c", "#include \"term.h\"\nint main(void) { return 0; }\n");
	write(folder / "inc" / "term.h", "int term(void);\n");
	write(folder / "Makefile", "CFLAGS = -Wall -std=c99 -Iinc -I$(PREFIX)/include\n");

	const ProjectProposal proposal{detect(folder)};

	CHECK(proposal.sourceGroupType == SourceGroupType::C_EMPTY);
	CHECK(proposal.reasons.front() == "Found Makefile, which is not read: an empty C group on the folder (1 .c file and 1 .h file)");
	CHECK(strings(proposal, "extensions") == Strings{".c"});
	CHECK(strings(proposal, "header_search_paths") == Strings{generic(folder / "inc")});
	if (std::ranges::contains(ClangCompiler::getAvailableCStandards(), "c99"))
	{
		CHECK(text(proposal, "c_standard") == "c99");
	}
	CHECK(!proposal.fields.contains("exclude_filters"));
}

TEST_CASE("ProjectDetector proposes C for mostly C files, with the folder's headers")
{
	const TestTempDirectory temp{"detectMostlyC"};
	const fs::path folder{temp.getPath() / "single"};
	write(folder / "single_image.h", "int load(const char* name);\n");
	write(folder / "tests" / "a.c", "#include \"single_image.h\"\n");
	write(folder / "tests" / "b.c", "int b(void) { return 0; }\n");
	write(folder / "tests" / "c.cpp", "int c() { return 0; }\n");
	write(folder / "build-aux" / "gen.c", "int gen(void) { return 0; }\n");	// scripts, not a build tree
	write(folder / "build-debug" / "build.ninja");	// a build tree
	write(folder / "build-debug" / "x.c", "int x(void) { return 0; }\n");

	const ProjectProposal proposal{detect(folder)};

	CHECK(proposal.sourceGroupType == SourceGroupType::C_EMPTY);
	CHECK(proposal.reasons.front() == "No build system found: an empty C group on the folder (1 C++ source, 3 .c files and 1 .h file)");
	CHECK(strings(proposal, "header_search_paths") == Strings{generic(folder)});
	CHECK(strings(proposal, "exclude_filters") == Strings{generic(folder / "build-debug")});
	CHECK(mentions(proposal.reasons, "The 1 C++ source file is not in this C group"));
}

TEST_CASE("ProjectDetector notes Meson as not detected yet")
{
	const TestTempDirectory temp{"detectMeson"};
	const fs::path folder{temp.getPath() / "mesonproj"};
	write(folder / "meson.build", "project('m', 'c', default_options: ['c_std=c11'])\n");
	write(folder / "src" / "m.c", "int m(void) { return 0; }\n");

	const ProjectProposal proposal{detect(folder)};

	CHECK(proposal.sourceGroupType == SourceGroupType::C_EMPTY);
	CHECK(mentions(proposal.notices, "Meson (meson.build) is not detected yet"));
	if (std::ranges::contains(ClangCompiler::getAvailableCStandards(), "c11"))
	{
		CHECK(text(proposal, "c_standard") == "c11");
	}
}

TEST_CASE("ProjectDetector reads a git repository's branch from .git")
{
	const TestTempDirectory temp{"detectGitDir"};
	const fs::path root{temp.getPath() / "repo"};
	write(root / ".git" / "HEAD", "ref: refs/heads/feature/x\n");
	write(root / "src" / "a.cpp", "int a() { return 0; }\n");

	const auto git{detectGitRepository(generic(root / "src"))};
	REQUIRE(git.has_value());
	CHECK(git->root == generic(root));
	CHECK(git->name == "repo");
	CHECK(git->kind == GitRepositoryInfo::Kind::Repository);
	CHECK(git->branch == "feature/x");

	const ProjectProposal proposal{detect(root / "src")};
	CHECK(mentions(proposal.notices, "Git repository repo, branch feature/x (the folder is src in it)"));
}

TEST_CASE("ProjectDetector follows a .git file to a worktree's repository")
{
	const TestTempDirectory temp{"detectGitFile"};
	const fs::path main{temp.getPath() / "main"};
	const fs::path worktree{temp.getPath() / "wt"};
	write(main / ".git" / "worktrees" / "wt" / "HEAD", "0123456789abcdef0123456789abcdef01234567\n");
	write(worktree / ".git", "gitdir: ../main/.git/worktrees/wt\n");

	const auto git{detectGitRepository(generic(worktree))};
	REQUIRE(git.has_value());
	CHECK(git->root == generic(worktree));
	CHECK(git->kind == GitRepositoryInfo::Kind::Worktree);
	CHECK(git->branch.empty());
	CHECK(git->commit == "0123456789ab");

	const ProjectProposal proposal{detect(worktree)};
	CHECK(mentions(proposal.notices, "Git worktree wt, detached at 0123456789ab"));
}

TEST_CASE("ProjectDetector lists git submodules and whether they are checked out")
{
	const TestTempDirectory temp{"detectSubmodules"};
	const fs::path root{temp.getPath() / "super"};
	write(root / ".git" / "HEAD", "ref: refs/heads/main\n");
	write(root / ".gitmodules", R"([submodule "fmt"]
	path = third_party/fmt
	url = https://example.com/fmt.git
[submodule "json"]
	path = third_party/json
	url = https://example.com/json.git
)");
	write(root / "third_party" / "fmt" / ".git", "gitdir: ../../.git/modules/fmt\n");
	write(root / "third_party" / "fmt" / "CMakeLists.txt", "project(fmt)\n");
	fs::create_directories(root / "third_party" / "json");	// not initialised
	write(root / "CMakeLists.txt", "project(super)\nadd_subdirectory(third_party/fmt)\n");

	const std::vector<GitSubmodule> submodules{readGitSubmodules(generic(root))};
	REQUIRE(submodules.size() == 2);
	CHECK(submodules[0].path == "third_party/fmt");
	CHECK(submodules[0].checkedOut);
	CHECK(submodules[1].path == "third_party/json");
	CHECK(!submodules[1].checkedOut);

	const ProjectProposal proposal{detect(root)};
	CHECK(mentions(proposal.notices, "2 git submodules: third_party/fmt and third_party/json; not checked out: third_party/json"));
	// The build compiles the checked-out submodule: it is not excluded.
	CHECK(!proposal.fields.contains("exclude_filters"));
	CHECK(mentions(proposal.reasons, "third_party/ holds dependencies the build compiles"));
}

TEST_CASE("ProjectDetector excludes vendored submodules an empty group does not build")
{
	const TestTempDirectory temp{"detectVendoredSubmodule"};
	const fs::path root{temp.getPath() / "plain"};
	write(root / ".git" / "HEAD", "ref: refs/heads/main\n");
	write(root / ".gitmodules", "[submodule \"x\"]\n\tpath = external/x\n");
	write(root / "external" / "x" / "x.cpp", "int x() { return 0; }\n");
	write(root / "main.cpp", "int main() {}\n");

	const ProjectProposal proposal{detect(root)};

	CHECK(proposal.sourceGroupType == SourceGroupType::CXX_EMPTY);
	CHECK(strings(proposal, "exclude_filters") == Strings{generic(root / "external")});
	CHECK(mentions(proposal.notices, "1 git submodule: external/x, checked out"));
}
