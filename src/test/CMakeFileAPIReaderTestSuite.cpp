#include "Catch2.hpp"

#include <algorithm>
#include <filesystem>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "json-query/JSONQuery"

#include "CMakeFileAPIReader.h"
#include "FilePath.h"

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
