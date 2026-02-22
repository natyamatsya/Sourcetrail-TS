#include "Catch2.hpp"

#include <algorithm>
#include <filesystem>

#include "CMakeFileAPIReader.h"
#include "FilePath.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

// Returns the absolute path to the fixture build directory.
// Tests run with CWD = <build>/test/, so "data/..." resolves correctly.
FilePath fixtureBuildDir()
{
	return FilePath("data/CMakeFileAPIReaderTestSuite/build").makeAbsolute().makeCanonical();
}

FilePath fixtureSourceDir()
{
	return FilePath("data/CMakeFileAPIReaderTestSuite/source").makeAbsolute().makeCanonical();
}

bool containsPath(const std::vector<FilePath>& paths, const FilePath& needle)
{
	return std::any_of(paths.begin(), paths.end(), [&needle](const FilePath& p) {
		return p.str() == needle.str();
	});
}

bool containsSourceWithPath(
	const std::vector<CMakeFileAPIReader::SourceEntry>& entries, const FilePath& needle)
{
	return std::any_of(
		entries.begin(), entries.end(), [&needle](const CMakeFileAPIReader::SourceEntry& e) {
			return e.path.str() == needle.str();
		});
}

}	 // namespace

// ---------------------------------------------------------------------------
// discoverPresets — pure JSON parsing, no cmake subprocess
// ---------------------------------------------------------------------------

TEST_CASE("CMakeFileAPIReader discoverPresets finds visible presets")
{
	const auto presets = CMakeFileAPIReader::discoverPresets(fixtureSourceDir());

	REQUIRE(presets.size() == 2);
	REQUIRE(std::find(presets.begin(), presets.end(), "debug") != presets.end());
	REQUIRE(std::find(presets.begin(), presets.end(), "release") != presets.end());
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
			return e.path.str() == expectedPath.str();
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
			return e.path.str() == expectedPath.str();
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
			return e.path.str() == expectedPath.str();
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
			return e.path.str() == expectedPath.str();
		});
	REQUIRE(it != entries.end());
	REQUIRE(it->compileGroup.has_value());

	const FilePath expectedInclude =
		fixtureSourceDir().getConcatenated("/include").makeCanonical();
	CHECK(containsPath(it->compileGroup->includes, expectedInclude));
}

TEST_CASE("CMakeFileAPIReader getSources returns empty for unknown configuration")
{
	CMakeFileAPIReader reader{fixtureBuildDir()};
	const auto entries = reader.getSources("NonExistentConfig");

	CHECK(entries.empty());
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

TEST_CASE("CMakeFileAPIReader isReplyStale returns false for fresh reply")
{
	// The fixture reply was just generated; no input file is newer than the index.
	CMakeFileAPIReader reader{fixtureBuildDir()};
	CHECK_FALSE(reader.isReplyStale());
}

TEST_CASE("CMakeFileAPIReader isReplyStale returns false for missing reply")
{
	CMakeFileAPIReader reader{FilePath("/nonexistent/build/dir")};
	// No reply → not stale (caller should call ensureReply first).
	CHECK_FALSE(reader.isReplyStale());
}
