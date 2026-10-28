#include "Catch2.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "json-query/JSONQuery"

#include "CMakeFileAPIReader.h"
#ifndef SRCTRL_MODULE_BUILD
#include "FilePath.h"
#endif
#include "TestTempDirectory.h"

#if defined(_WIN32)
#define SOURCETRAIL_TEST_NULL_DEVICE "NUL"
#else
#define SOURCETRAIL_TEST_NULL_DEVICE "/dev/null"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.file;
#endif

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

FilePath fixtureRootDir()
{
	return FilePath{__FILE__}
		.makeAbsolute()
		.makeCanonical()
		.getParentDirectory()
		.getParentDirectory()
		.getParentDirectory()
		.getConcatenated("/bin/test/data/CMakeFileAPIReaderTestSuite")
		.makeCanonical();
}

// Returns the absolute path to the fixture build directory.
FilePath fixtureBuildDir()
{
	return fixtureRootDir().getConcatenated("/build").makeCanonical();
}

FilePath fixtureSourceDir()
{
	return fixtureRootDir().getConcatenated("/source").makeCanonical();
}

bool pathsMatchForFixture(const FilePath& lhs, const FilePath& rhs)
{
	if (lhs.str() == rhs.str())
		return true;

	const std::string marker{"/CMakeFileAPIReaderTestSuite/"};
	const std::string lhsString{lhs.str()};
	const std::string rhsString{rhs.str()};
	const auto lhsPos{lhsString.find(marker)};
	const auto rhsPos{rhsString.find(marker)};
	if (lhsPos == std::string::npos || rhsPos == std::string::npos)
		return false;
	return lhsString.substr(lhsPos) == rhsString.substr(rhsPos);
}

bool containsPath(const std::vector<FilePath>& paths, const FilePath& needle)
{
	return std::any_of(paths.begin(), paths.end(), [&needle](const FilePath& p) {
		return pathsMatchForFixture(p, needle);
	});
}

bool containsSourceWithPath(
	const std::vector<CMakeFileAPIReader::SourceEntry>& entries, const FilePath& needle)
{
	return std::any_of(
		entries.begin(), entries.end(), [&needle](const CMakeFileAPIReader::SourceEntry& e) {
			return pathsMatchForFixture(e.path, needle);
		});
}

}	 // namespace

// ---------------------------------------------------------------------------
// resolveBinaryDir — pure CMakePresets.json parsing, no cmake subprocess
// ---------------------------------------------------------------------------

TEST_CASE("CMakeFileAPIReader resolveBinaryDir resolves debug preset")
{
	FilePath buildDir =
		CMakeFileAPIReader::resolveBinaryDir(fixtureSourceDir(), "debug");

	// CMakePresets.json: binaryDir = "${sourceDir}/../build"
	FilePath expected =
		fixtureSourceDir().getConcatenated("/../build").makeCanonical();
	REQUIRE(buildDir.makeCanonical().str() == expected.str());
}

TEST_CASE("CMakeFileAPIReader resolveBinaryDir resolves release preset")
{
	FilePath buildDir =
		CMakeFileAPIReader::resolveBinaryDir(fixtureSourceDir(), "release");

	FilePath expected =
		fixtureSourceDir().getConcatenated("/../build").makeCanonical();
	REQUIRE(buildDir.makeCanonical().str() == expected.str());
}

TEST_CASE("CMakeFileAPIReader resolveBinaryDir returns empty for unknown preset")
{
	const FilePath buildDir =
		CMakeFileAPIReader::resolveBinaryDir(fixtureSourceDir(), "nonexistent-preset");

	REQUIRE(buildDir.empty());
}

TEST_CASE("CMakeFileAPIReader resolveBinaryDir follows inherits chain")
{
	// "inherited" has no binaryDir itself — it inherits from "hidden-base"
	FilePath buildDir =
		CMakeFileAPIReader::resolveBinaryDir(fixtureSourceDir(), "inherited");

	FilePath expected =
		fixtureSourceDir().getConcatenated("/../build").makeCanonical();
	REQUIRE(buildDir.makeCanonical().str() == expected.str());
}

TEST_CASE("CMakeFileAPIReader resolveBinaryDir returns empty for missing source dir")
{
	const FilePath buildDir = CMakeFileAPIReader::resolveBinaryDir(
		FilePath("/nonexistent/path"), "debug");

	REQUIRE(buildDir.empty());
}

// ---------------------------------------------------------------------------
// discoverPresets — pure JSON parsing, no cmake subprocess
// ---------------------------------------------------------------------------

TEST_CASE("CMakeFileAPIReader discoverPresets finds visible presets")
{
	const auto presets = CMakeFileAPIReader::discoverPresets(fixtureSourceDir());

	REQUIRE(presets.size() == 3);
	REQUIRE(std::find(presets.begin(), presets.end(), "debug") != presets.end());
	REQUIRE(std::find(presets.begin(), presets.end(), "release") != presets.end());
	REQUIRE(std::find(presets.begin(), presets.end(), "inherited") != presets.end());
}

TEST_CASE("CMakeFileAPIReader discoverPresets excludes hidden presets")
{
	const auto presets = CMakeFileAPIReader::discoverPresets(fixtureSourceDir());

	REQUIRE(std::find(presets.begin(), presets.end(), "hidden-base") == presets.end());
}

TEST_CASE("CMakeFileAPIReader discoverPresets returns empty for missing source dir")
{
	const auto presets =
		CMakeFileAPIReader::discoverPresets(FilePath("/nonexistent/path/that/does/not/exist"));

	REQUIRE(presets.empty());
}

namespace
{
// A source directory whose CMakePresets.json is `presetsJson`.
void writePresets(const std::filesystem::path& sourceDir, const std::string& presetsJson)
{
	std::filesystem::create_directories(sourceDir);
	std::ofstream{sourceDir / "CMakePresets.json"} << presetsJson;
}

std::string hostSystemName()
{
#if defined(_WIN32)
	return "Windows";
#elif defined(__APPLE__)
	return "Darwin";
#else
	return "Linux";
#endif
}
}	 // namespace

TEST_CASE("CMakeFileAPIReader discoverPresetInfos keeps file order, names and descriptions")
{
	const TestTempDirectory tempDirectory{"presetInfos"};
	writePresets(tempDirectory.getPath(), R"({"version": 6, "configurePresets": [
		{"name": "zeta", "displayName": "Zeta build", "description": "The last letter", "binaryDir": "z"},
		{"name": "alpha", "binaryDir": "a"}
	]})");

	const auto presets{
		CMakeFileAPIReader::discoverPresetInfos(FilePath{tempDirectory.getPath().string()})};

	REQUIRE(presets.size() == 2);
	CHECK(presets[0].name == "zeta");	// as written, not sorted
	CHECK(presets[0].displayName == "Zeta build");
	CHECK(presets[0].description == "The last letter");
	CHECK(presets[1].name == "alpha");
	CHECK(presets[1].displayName.empty());
}

TEST_CASE("CMakeFileAPIReader discoverPresetInfos gives the generator and the environment that is not set")
{
	const TestTempDirectory tempDirectory{"presetGenerator"};
	writePresets(tempDirectory.getPath(), R"({"version": 6, "configurePresets": [
		{"name": "base", "hidden": true, "generator": "Ninja", "environment": {"OWN_ROOT": "/opt/own"}},
		{"name": "child", "inherits": "base", "binaryDir": "$env{OWN_ROOT}/b",
		 "toolchainFile": "$env{SOURCETRAIL_TEST_UNSET_A}/tc.cmake",
		 "cacheVariables": {"X": {"type": "PATH", "value": "$penv{SOURCETRAIL_TEST_UNSET_B}"}, "Y": "$penv{OWN_ROOT}"}},
		{"name": "plain", "binaryDir": "b"}
	]})");

	const auto presets{
		CMakeFileAPIReader::discoverPresetInfos(FilePath{tempDirectory.getPath().string()})};

	REQUIRE(presets.size() == 2);
	CHECK(presets[0].generator == "Ninja");	// inherited
	// $penv{} reads only the process environment, which has no OWN_ROOT.
	CHECK(
		presets[0].undefinedEnvironment ==
		std::vector<std::string>{"SOURCETRAIL_TEST_UNSET_A", "SOURCETRAIL_TEST_UNSET_B", "OWN_ROOT"});
	CHECK(presets[1].generator.empty());
	CHECK(presets[1].undefinedEnvironment.empty());
}

TEST_CASE("CMakeFileAPIReader discoverPresets leaves out presets whose condition is false")
{
	const TestTempDirectory tempDirectory{"presetConditions"};
	const std::string host{hostSystemName()};
	writePresets(tempDirectory.getPath(), R"({"version": 6, "configurePresets": [
		{"name": "here", "binaryDir": "b", "condition": {"type": "equals", "lhs": "${hostSystemName}", "rhs": ")" + host + R"("}},
		{"name": "elsewhere", "binaryDir": "b", "condition": {"type": "notEquals", "lhs": "${hostSystemName}", "rhs": ")" + host + R"("}},
		{"name": "off-base", "hidden": true, "condition": false},
		{"name": "inherits-off", "inherits": "off-base", "binaryDir": "b"},
		{"name": "any", "binaryDir": "b", "condition": {"type": "anyOf", "conditions": [
			{"type": "const", "value": false},
			{"type": "inList", "string": "${presetName}", "list": ["x", "any"]}]}},
		{"name": "never", "binaryDir": "b", "condition": {"type": "not", "condition": {"type": "matches", "string": "abc", "regex": "^a"}}}
	]})");

	const auto presets{
		CMakeFileAPIReader::discoverPresets(FilePath{tempDirectory.getPath().string()})};

	CHECK(presets == std::vector<std::string>{"here", "any"});
}

TEST_CASE("CMakeFileAPIReader resolveBinaryDir expands environment and directory macros")
{
	const TestTempDirectory tempDirectory{"presetMacros"};
	const std::filesystem::path sourceDir{tempDirectory.getPath() / "source"};
	writePresets(sourceDir, R"({"version": 6, "configurePresets": [
		{"name": "env-base", "hidden": true, "environment": {"OUT_ROOT": "${sourceParentDir}/out"}},
		{"name": "env", "inherits": "env-base", "binaryDir": "$env{OUT_ROOT}/${presetName}"},
		{"name": "parent", "binaryDir": "${sourceParentDir}/build-${sourceDirName}"},
		{"name": "relative", "binaryDir": "build/../build/rel"}
	]})");
	const FilePath source{sourceDir.string()};
	// Generic form (forward slashes), as FilePath gives it on Windows too.
	const std::string root{tempDirectory.getPath().generic_string()};

	CHECK(CMakeFileAPIReader::resolveBinaryDir(source, "env").str() == root + "/out/env");
	CHECK(CMakeFileAPIReader::resolveBinaryDir(source, "parent").str() == root + "/build-source");
	CHECK(
		CMakeFileAPIReader::resolveBinaryDir(source, "relative").str() ==
		sourceDir.generic_string() + "/build/rel");
}

TEST_CASE("CMakeFileAPIReader splitCompileFragment unquotes as the generator's shell")
{
#if defined(_WIN32)
	CHECK(
		CMakeFileAPIReader::splitCompileFragment(R"(-O2 "-DNAME=a b" /std:c++17)") ==
		std::vector<std::string>{"-O2", "-DNAME=a b", "/std:c++17"});
#else
	// The Xcode generator quotes with single quotes, Ninja and Makefiles with double quotes.
	CHECK(
		CMakeFileAPIReader::splitCompileFragment(R"(-O3 -DNDEBUG '-std=c++17' -fvisibility=hidden)") ==
		std::vector<std::string>{"-O3", "-DNDEBUG", "-std=c++17", "-fvisibility=hidden"});
	CHECK(
		CMakeFileAPIReader::splitCompileFragment(R"(  "-DMSG=\"hi there\"" -DA=b\ c '' x)") ==
		std::vector<std::string>{"-DMSG=\"hi there\"", "-DA=b c", "", "x"});
#endif
	CHECK(CMakeFileAPIReader::splitCompileFragment("   ").empty());
}

TEST_CASE("CMakeFileAPIReader matchesTargetFilter takes several patterns")
{
	CHECK(CMakeFileAPIReader::matchesTargetFilter("core", ""));
	CHECK(CMakeFileAPIReader::matchesTargetFilter("core", "core"));
	CHECK(CMakeFileAPIReader::matchesTargetFilter("core", "app; core"));
	CHECK(CMakeFileAPIReader::matchesTargetFilter("net_tests", "app,*_tests"));
	CHECK_FALSE(CMakeFileAPIReader::matchesTargetFilter("core", "app;net*"));
}

// ---------------------------------------------------------------------------
// hasReply — checks for the pre-baked reply in the fixture build dir
// ---------------------------------------------------------------------------

TEST_CASE("CMakeFileAPIReader hasReply returns true for fixture build dir")
{
	CMakeFileAPIReader reader{fixtureBuildDir()};
	REQUIRE(reader.hasReply());
}

TEST_CASE("CMakeFileAPIReader hasReply returns false for empty dir")
{
	CMakeFileAPIReader reader{FilePath("/nonexistent/build/dir")};
	REQUIRE_FALSE(reader.hasReply());
}

// ---------------------------------------------------------------------------
// isConfigured / lastConfigureFailed — what a configure left in the build dir
// ---------------------------------------------------------------------------

namespace
{
void writeFile(const std::filesystem::path& path)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream{path} << "{}\n";
}
}	 // namespace

TEST_CASE("CMakeFileAPIReader isConfigured is false after a configure that failed")
{
	// leveldb without its googletest submodule: the configure fails, and CMakeCache.txt and
	// CMakeFiles/ are there, but no build file.
	const TestTempDirectory temp{"CMakeFileAPIReaderFailedConfigure"};
	const std::filesystem::path buildDir{temp.getPath()};
	writeFile(buildDir / "CMakeCache.txt");
	writeFile(buildDir / "CMakeFiles" / "CMakeConfigureLog.yaml");
	const CMakeFileAPIReader reader{FilePath{buildDir.generic_string()}};

	CHECK_FALSE(reader.lastConfigureFailed());	// CMake before writing error replies
	CHECK_FALSE(reader.isConfigured());

	// CMake that writes an error reply says so.
	writeFile(buildDir / ".cmake/api/v1/reply/error-2026-10-28T03-39-29-0580.json");
	CHECK(reader.lastConfigureFailed());
	CHECK_FALSE(reader.isConfigured());
}

TEST_CASE("CMakeFileAPIReader isConfigured is true for a generated build directory")
{
	const TestTempDirectory temp{"CMakeFileAPIReaderGeneratedBuildDir"};
	const std::filesystem::path buildDir{temp.getPath()};
	writeFile(buildDir / "CMakeCache.txt");
	const CMakeFileAPIReader reader{FilePath{buildDir.generic_string()}};
	CHECK_FALSE(reader.isConfigured());

	SECTION("by Ninja")
	{
		writeFile(buildDir / "build.ninja");
		CHECK(reader.isConfigured());
	}
	SECTION("by Xcode")
	{
		std::filesystem::create_directories(buildDir / "app.xcodeproj");
		CHECK(reader.isConfigured());
	}
	SECTION("with a reply, until a later configure fails")
	{
		writeFile(buildDir / ".cmake/api/v1/reply/index-2026-10-28T03-00-00-0000.json");
		CHECK(reader.isConfigured());
		CHECK_FALSE(reader.lastConfigureFailed());

		// An error reply older than the index is from a configure that failed before.
		writeFile(buildDir / ".cmake/api/v1/reply/error-2026-10-27T03-00-00-0000.json");
		CHECK(reader.isConfigured());

		writeFile(buildDir / ".cmake/api/v1/reply/error-2026-10-28T04-00-00-0000.json");
		CHECK(reader.lastConfigureFailed());
		CHECK_FALSE(reader.isConfigured());
	}
}

TEST_CASE("CMakeFileAPIReader getJsonEntryPoints returns codemodel entry")
{
	CMakeFileAPIReader reader{fixtureBuildDir()};
	const auto entryPoints{reader.getJsonEntryPoints()};

	REQUIRE_FALSE(entryPoints.empty());

	const bool hasCodemodel{std::any_of(
		entryPoints.begin(), entryPoints.end(), [](const CMakeFileAPIReader::JsonEntryPoint& entryPoint)
		{
			if (entryPoint.kind != "codemodel")
				return false;
			return entryPoint.path.exists();
		})};
	CHECK(hasCodemodel);
}

// ---------------------------------------------------------------------------
// getSources — reads the pre-baked codemodel reply
// ---------------------------------------------------------------------------

TEST_CASE("CMakeFileAPIReader getSources returns hello.cpp")
{
	CMakeFileAPIReader reader{fixtureBuildDir()};
	const auto entries = reader.getSources();

	REQUIRE_FALSE(entries.empty());

	const FilePath expectedPath =
		fixtureSourceDir().getConcatenated("/src/hello.cpp").makeCanonical();
	REQUIRE(containsSourceWithPath(entries, expectedPath));
}

TEST_CASE("CMakeFileAPIReader getSources entry has correct target metadata")
{
	CMakeFileAPIReader reader{fixtureBuildDir()};
	const auto entries = reader.getSources();

	REQUIRE_FALSE(entries.empty());

	const FilePath expectedPath =
		fixtureSourceDir().getConcatenated("/src/hello.cpp").makeCanonical();

	auto it = std::find_if(
		entries.begin(), entries.end(), [&expectedPath](const CMakeFileAPIReader::SourceEntry& e) {
			return pathsMatchForFixture(e.path, expectedPath);
		});
	REQUIRE(it != entries.end());

	CHECK(it->targetName == "hello");
	CHECK(it->targetType == "EXECUTABLE");
	CHECK_FALSE(it->isGenerated);
}

TEST_CASE("CMakeFileAPIReader getSources entry has compile group with CXX language")
{
	CMakeFileAPIReader reader{fixtureBuildDir()};
	const auto entries = reader.getSources();

	const FilePath expectedPath =
		fixtureSourceDir().getConcatenated("/src/hello.cpp").makeCanonical();

	auto it = std::find_if(
		entries.begin(), entries.end(), [&expectedPath](const CMakeFileAPIReader::SourceEntry& e) {
			return pathsMatchForFixture(e.path, expectedPath);
		});
	REQUIRE(it != entries.end());
	REQUIRE(it->compileGroup.has_value());

	CHECK(it->compileGroup->language == "CXX");
}

TEST_CASE("CMakeFileAPIReader getSources entry has GREETING define")
{
	CMakeFileAPIReader reader{fixtureBuildDir()};
	const auto entries = reader.getSources();

	const FilePath expectedPath =
		fixtureSourceDir().getConcatenated("/src/hello.cpp").makeCanonical();

	auto it = std::find_if(
		entries.begin(), entries.end(), [&expectedPath](const CMakeFileAPIReader::SourceEntry& e) {
			return pathsMatchForFixture(e.path, expectedPath);
		});
	REQUIRE(it != entries.end());
	REQUIRE(it->compileGroup.has_value());

	const auto& defines = it->compileGroup->defines;
	const bool hasGreeting = std::any_of(defines.begin(), defines.end(), [](const std::string& d) {
		return d.find("GREETING") != std::string::npos;
	});
	CHECK(hasGreeting);
}

TEST_CASE("CMakeFileAPIReader getSources entry has include path")
{
	CMakeFileAPIReader reader{fixtureBuildDir()};
	const auto entries = reader.getSources();

	const FilePath expectedPath =
		fixtureSourceDir().getConcatenated("/src/hello.cpp").makeCanonical();

	auto it = std::find_if(
		entries.begin(), entries.end(), [&expectedPath](const CMakeFileAPIReader::SourceEntry& e) {
			return pathsMatchForFixture(e.path, expectedPath);
		});
	REQUIRE(it != entries.end());
	REQUIRE(it->compileGroup.has_value());

	const FilePath expectedInclude =
		fixtureSourceDir().getConcatenated("/include").makeCanonical();
	CHECK(containsPath(it->compileGroup->includes, expectedInclude));
}

TEST_CASE("CMakeFileAPIReader getSources returns module interface from CXX_MODULES file set")
{
	CMakeFileAPIReader reader{fixtureBuildDir()};
	const auto entries = reader.getSources();

	const FilePath expectedPath =
		fixtureSourceDir().getConcatenated("/src/hello.cppm").makeCanonical();
	CHECK(containsSourceWithPath(entries, expectedPath));
}

TEST_CASE("CMakeFileAPIReader getSources module interface has compile group with CXX language")
{
	CMakeFileAPIReader reader{fixtureBuildDir()};
	const auto entries = reader.getSources();

	const FilePath expectedPath =
		fixtureSourceDir().getConcatenated("/src/hello.cppm").makeCanonical();

	auto it = std::find_if(
		entries.begin(), entries.end(), [&expectedPath](const CMakeFileAPIReader::SourceEntry& e) {
			return pathsMatchForFixture(e.path, expectedPath);
		});
	REQUIRE(it != entries.end());
	REQUIRE(it->compileGroup.has_value());
	CHECK(it->compileGroup->language == "CXX");
}

TEST_CASE("CMakeFileAPIReader getSources returns empty for unknown configuration")
{
	CMakeFileAPIReader reader{fixtureBuildDir()};
	const auto entries = reader.getSources("NonExistentConfig");

	CHECK(entries.empty());
}

TEST_CASE("CMakeFileAPIReader getSourcesDetailed returns entries")
{
	CMakeFileAPIReader reader{fixtureBuildDir()};
	const auto sourcesResult{reader.getSourcesDetailed()};

	REQUIRE(sourcesResult.has_value());
	CHECK_FALSE(sourcesResult->entries.empty());
}

TEST_CASE("CMakeFileAPIReader getSourcesDetailed returns target metadata")
{
	CMakeFileAPIReader reader{fixtureBuildDir()};
	const auto sourcesResult{reader.getSourcesDetailed()};

	REQUIRE(sourcesResult.has_value());
	REQUIRE_FALSE(sourcesResult->targets.empty());

	const auto targetIt = std::find_if(
		sourcesResult->targets.begin(),
		sourcesResult->targets.end(),
		[](const CMakeFileAPIReader::TargetEntry& target)
		{
			return target.name == "hello";
		});
	REQUIRE(targetIt != sourcesResult->targets.end());

	CHECK(targetIt->type == "EXECUTABLE");
	CHECK(pathsMatchForFixture(targetIt->sourceDir, fixtureSourceDir()));
	CHECK(targetIt->dependencies.empty());
}

TEST_CASE("CMakeFileAPIReader getSourcesDetailed returns typed configuration error")
{
	CMakeFileAPIReader reader{fixtureBuildDir()};
	const auto sourcesResult{reader.getSourcesDetailed("NonExistentConfig")};

	REQUIRE_FALSE(sourcesResult.has_value());
	CHECK(
		sourcesResult.error().code ==
		CMakeFileAPIReader::GetSourcesErrorCode::ConfigurationNotFound);
	CHECK_FALSE(sourcesResult.error().message.empty());
}

TEST_CASE("CMakeFileAPIReader getSources target glob filters by name")
{
	CMakeFileAPIReader reader{fixtureBuildDir()};

	const auto all = reader.getSources({}, {});
	const auto matched = reader.getSources({}, "hello");
	const auto noMatch = reader.getSources({}, "nonexistent*");

	CHECK_FALSE(all.empty());
	CHECK(matched.size() == all.size());
	CHECK(noMatch.empty());
}

// ---------------------------------------------------------------------------
// getCMakeInputFiles — reads the pre-baked cmakeFiles reply
// ---------------------------------------------------------------------------

TEST_CASE("CMakeFileAPIReader getCMakeInputFiles returns CMakeLists.txt")
{
	CMakeFileAPIReader reader{fixtureBuildDir()};
	const auto inputs = reader.getCMakeInputFiles();

	REQUIRE_FALSE(inputs.empty());

	const FilePath expectedCMakeLists =
		fixtureSourceDir().getConcatenated("/CMakeLists.txt").makeCanonical();
	CHECK(containsPath(inputs, expectedCMakeLists));
}

TEST_CASE("CMakeFileAPIReader getCMakeInputFiles returns empty for missing reply")
{
	CMakeFileAPIReader reader{FilePath("/nonexistent/build/dir")};
	const auto inputs = reader.getCMakeInputFiles();

	CHECK(inputs.empty());
}

// ---------------------------------------------------------------------------
// isReplyStale — mtime-based staleness detection
// ---------------------------------------------------------------------------

TEST_CASE("CMakeFileAPIReader isReplyStale compares input mtimes against the index")
{
	// The checked-in fixture has uncontrolled mtimes (git checkout order, absolute
	// paths into the generating machine), so build a minimal hermetic reply where
	// the relative mtimes are set explicitly.
	namespace fs = std::filesystem;
	const TestTempDirectory tempDirectory{"isReplyStale"};
	const fs::path& root = tempDirectory.getPath();
	const fs::path replyDir = root / "build/.cmake/api/v1/reply";
	fs::create_directories(replyDir);
	const fs::path sourceDir = root / "source";
	fs::create_directories(sourceDir);

	const fs::path input = sourceDir / "CMakeLists.txt";
	std::ofstream{input} << "project(hermetic)\n";

	// generic_string(): a Windows path's backslashes would be invalid JSON escapes.
	std::ofstream{replyDir / "cmakeFiles-v1-test.json"}
		<< R"({"kind":"cmakeFiles","paths":{"source":")" << sourceDir.generic_string()
		<< R"("},"inputs":[{"path":"CMakeLists.txt"}]})";
	std::ofstream{replyDir / "index-test.json"}
		<< R"({"objects":[{"kind":"cmakeFiles","jsonFile":"cmakeFiles-v1-test.json"}]})";

	const fs::path index = replyDir / "index-test.json";
	CMakeFileAPIReader reader{FilePath{(root / "build").string()}};

	// Index newer than the input -> fresh.
	fs::last_write_time(index, fs::last_write_time(input) + std::chrono::seconds{2});
	CHECK_FALSE(reader.isReplyStale());

	// Input newer than the index -> stale.
	fs::last_write_time(input, fs::last_write_time(index) + std::chrono::seconds{2});
	CHECK(reader.isReplyStale());
}

TEST_CASE("CMakeFileAPIReader isReplyStale counts a changed presets file")
{
	namespace fs = std::filesystem;
	const TestTempDirectory tempDirectory{"isReplyStalePresets"};
	const fs::path& root = tempDirectory.getPath();
	const fs::path replyDir = root / "build/.cmake/api/v1/reply";
	fs::create_directories(replyDir);
	const fs::path sourceDir = root / "source";
	writePresets(sourceDir, R"({"version": 6, "configurePresets": []})");
	std::ofstream{replyDir / "index-test.json"} << R"({"objects":[]})";

	const fs::path index = replyDir / "index-test.json";
	const fs::path presets = sourceDir / "CMakePresets.json";
	CMakeFileAPIReader reader{FilePath{(root / "build").string()}};
	const FilePath source{sourceDir.string()};

	fs::last_write_time(index, fs::last_write_time(presets) + std::chrono::seconds{2});
	CHECK_FALSE(reader.isReplyStale(source));

	fs::last_write_time(presets, fs::last_write_time(index) + std::chrono::seconds{2});
	CHECK(reader.isReplyStale(source));
	CHECK_FALSE(reader.isReplyStale());	// without the source dir only CMake's inputs count
}

// ---------------------------------------------------------------------------
// ensureReply -- runs the real cmake on a project without languages (fast)
// ---------------------------------------------------------------------------

namespace
{
bool hasCMake()
{
	return std::system("cmake --version > " SOURCETRAIL_TEST_NULL_DEVICE " 2>&1") == 0;
}
}	 // namespace

TEST_CASE("CMakeFileAPIReader ensureReply reconfigures a stale reply when asked")
{
	if (!hasCMake())
		SKIP("cmake is not on PATH");

	namespace fs = std::filesystem;
	const TestTempDirectory tempDirectory{"ensureReply"};
	const fs::path sourceDir = tempDirectory.getPath() / "source";
	writePresets(sourceDir, R"({"version": 3, "configurePresets": [
		{"name": "dev", "binaryDir": "${sourceDir}/../build"}]})");
	std::ofstream{sourceDir / "CMakeLists.txt"}
		<< "cmake_minimum_required(VERSION 3.21)\nproject(probe NONE)\nadd_custom_target(first)\n";

	const FilePath source{sourceDir.string()};
	const FilePath buildDir{CMakeFileAPIReader::resolveBinaryDir(source, "dev")};
	CMakeFileAPIReader reader{buildDir};
	REQUIRE(reader.ensureReply({}, source, "dev").has_value());
	REQUIRE(reader.hasReply());
	// One with a single-config generator, several with Visual Studio's (Windows' default).
	CHECK_FALSE(reader.getConfigurations().empty());

	const auto targetNames = [&reader]() {
		std::vector<std::string> names;
		for (const auto& target : reader.getSourcesDetailed()->targets)
			names.push_back(target.name);
		return names;
	};
	CHECK(targetNames() == std::vector<std::string>{"first"});

	std::ofstream{sourceDir / "CMakeLists.txt", std::ios::app} << "add_custom_target(second)\n";
	// A reply exists: without reconfigure it is used as it is...
	REQUIRE(reader.ensureReply({}, source, "dev").has_value());
	CHECK(targetNames().size() == 1);
	// ...with it, CMake runs again and the reply has the new target.
	REQUIRE(reader.ensureReply({}, source, "dev", true).has_value());
	const auto names{targetNames()};
	CHECK(std::find(names.begin(), names.end(), "second") != names.end());
}

TEST_CASE("CMakeFileAPIReader ensureReply configures a build directory without a preset")
{
	if (!hasCMake())
		SKIP("cmake is not on PATH");

	namespace fs = std::filesystem;
	const TestTempDirectory tempDirectory{"ensureReplyNoPreset"};
	const fs::path sourceDir = tempDirectory.getPath() / "source";
	fs::create_directories(sourceDir);	// no CMakePresets.json
	std::ofstream{sourceDir / "CMakeLists.txt"}
		<< "cmake_minimum_required(VERSION 3.21)\nproject(probe NONE)\nadd_custom_target(only)\n";

	const FilePath buildDir{(tempDirectory.getPath() / "elsewhere" / "build").string()};
	CMakeFileAPIReader reader{buildDir};
	REQUIRE(reader.ensureReply({}, FilePath{sourceDir.string()}, {}).has_value());
	REQUIRE(reader.hasReply());	// cmake -S <source> -B <build> made the directory
	CHECK(reader.getSourcesDetailed()->targets.size() == 1);
}

TEST_CASE("CMakeFileAPIReader ensureReply returns CMake's error when configuring fails")
{
	if (!hasCMake())
		SKIP("cmake is not on PATH");

	namespace fs = std::filesystem;
	const TestTempDirectory tempDirectory{"ensureReplyFails"};
	const fs::path sourceDir = tempDirectory.getPath() / "source";
	writePresets(sourceDir, R"({"version": 3, "configurePresets": [
		{"name": "dev", "binaryDir": "${sourceDir}/../build"}]})");
	std::ofstream{sourceDir / "CMakeLists.txt"}
		<< "cmake_minimum_required(VERSION 3.21)\nproject(probe NONE)\n"
		   "message(FATAL_ERROR \"the probe refuses to configure\")\n";

	const FilePath source{sourceDir.string()};
	CMakeFileAPIReader reader{CMakeFileAPIReader::resolveBinaryDir(source, "dev")};
	const auto result{reader.ensureReply({}, source, "dev")};
	REQUIRE_FALSE(result.has_value());
	CHECK(result.error().code == CMakeFileAPIReader::ConfigureErrorCode::CMakeFailed);
	CHECK(result.error().message.find("the probe refuses to configure") != std::string::npos);
}

TEST_CASE("CMakeFileAPIReader isReplyStale returns false for missing reply")
{
	CMakeFileAPIReader reader{FilePath("/nonexistent/build/dir")};
	// No reply → not stale (caller should call ensureReply first).
	CHECK_FALSE(reader.isReplyStale());
}

// ---------------------------------------------------------------------------
// Regression: JSONPath evaluate must not corrupt subsequent QJsonObject reads
// ---------------------------------------------------------------------------

// Reproduces the bug where json-query's JSONPath::evaluate() on one document
// corrupts Qt's CBOR shared-data pool, causing QJsonValue::type() to report
// Array instead of Object for elements of a separately-parsed document's array.
TEST_CASE("JSONPath evaluate does not corrupt codemodel configurations array")
{
	using namespace json_query;

	const FilePath replyDir =
		fixtureBuildDir().getConcatenated("/.cmake/api/v1/reply");

	// Step 1: read the index and evaluate JSONPath on it (the corrupting op).
	const FilePath indexPath = [&]
	{
		const QDir qdir{QString::fromStdString(replyDir.str())};
		const auto entries = qdir.entryList({"index-*.json"}, QDir::Files, QDir::Name);
		REQUIRE_FALSE(entries.isEmpty());
		return replyDir.getConcatenated("/" + entries.last().toStdString());
	}();

	QFile indexFile{QString::fromStdString(indexPath.str())};
	REQUIRE(indexFile.open(QIODevice::ReadOnly));
	const auto indexDoc = QJsonDocument::fromJson(indexFile.readAll());
	REQUIRE(indexDoc.isObject());

	const auto pathResult = JSONPath::create(u"$.objects[?(@.kind == \"codemodel\")].jsonFile");
	REQUIRE(pathResult.has_value());
	const auto codemodelFiles = pathResult->evaluate(indexDoc);
	REQUIRE(codemodelFiles.has_value());
	REQUIRE_FALSE(codemodelFiles->isEmpty());
	const auto codemodelFilename = codemodelFiles->first().toString().toStdString();
	REQUIRE_FALSE(codemodelFilename.empty());

	// Step 2: read the codemodel document (parsed after JSONPath ran).
	const FilePath codemodelPath = replyDir.getConcatenated("/" + codemodelFilename);
	QFile cmFile{QString::fromStdString(codemodelPath.str())};
	REQUIRE(cmFile.open(QIODevice::ReadOnly));
	const auto codemodelDoc = QJsonDocument::fromJson(cmFile.readAll());
	REQUIRE(codemodelDoc.isObject());

	// Step 3: snapshot root before schema validation.
	const QJsonObject codemodelRoot = codemodelDoc.object();
	const QJsonArray configs = codemodelRoot["configurations"].toArray();
	REQUIRE(configs.size() >= 1);

	// Step 4: validate with JSONSchema (this is the other corrupting op).
	const QJsonObject schema{
		{"type", "object"},
		{"required", QJsonArray{"kind", "version", "configurations"}},
		{"properties",
		 QJsonObject{
			 {"kind", QJsonObject{{"type", "string"}, {"const", "codemodel"}}},
			 {"version",
			  QJsonObject{
				  {"type", "object"},
				  {"required", QJsonArray{"major"}},
				  {"properties",
				   QJsonObject{{"major", QJsonObject{{"type", "integer"}, {"minimum", 2}}}}}}},
			 {"configurations", QJsonObject{{"type", "array"}}},
		 }}};
	const auto jsonSchema = JSONSchema::create(QJsonValue{schema});
	REQUIRE(jsonSchema.has_value());
	const auto validation = jsonSchema->validate(codemodelRoot);
	CHECK(validation.isValid());

	// Step 5: verify the pre-snapshotted configs array is still intact.
	// Before the fix, configs[0].type() would return 4 (Array) instead of
	// 5 (Object) after JSONPath/JSONSchema had run.
	for (int i = 0; i < configs.size(); ++i)
	{
		INFO("configurations[" << i << "] type=" << static_cast<int>(configs.at(i).type()));
		CHECK(configs.at(i).type() == QJsonValue::Object);
		CHECK_FALSE(configs.at(i).toObject().isEmpty());
	}
}
