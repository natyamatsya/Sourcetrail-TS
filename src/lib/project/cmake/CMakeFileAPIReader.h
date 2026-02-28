#pragma once

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "FilePath.h"
#include "utilityExpected.h"

// Reads the CMake File-based API reply from a configured build directory.
//
// Usage:
//   1. Construct with the build directory path.
//   2. Call ensureReply() to write the query file and (optionally) trigger
//      a CMake reconfigure so the reply is generated.
//   3. Call getSources() to get the list of source files with compile info.
//   4. Call getCMakeInputFiles() to get files to watch for staleness.
//
// The query file is written to:
//   <buildDir>/.cmake/api/v1/query/client-sourcetrail/query.json
//
// The reply is read from:
//   <buildDir>/.cmake/api/v1/reply/
//
// Requires CMake >= 3.14.
class CMakeFileAPIReader
{
public:
	struct CompileGroup
	{
		std::string language;					// "CXX", "C"
		std::string compilerPath;               // Path to compiler executable
		FilePath sysroot;                       // Implicit sysroot if available
		std::vector<FilePath> includes;			// non-system include paths
		std::vector<FilePath> systemIncludes;	// system include paths
		std::vector<FilePath> frameworkSearchPaths; // framework search paths
		std::vector<std::string> defines;		// preprocessor defines
		std::vector<std::string> compileFlags;	// extra compiler flags
	};

	struct SourceEntry
	{
		FilePath path;
		bool isGenerated{false};
		std::optional<CompileGroup> compileGroup;
		std::string targetName;
		std::string targetType;	// EXECUTABLE, STATIC_LIBRARY, SHARED_LIBRARY, ...
		FilePath sourceDir;		// CMake source directory (paths.source from codemodel)
	};

	enum class GetSourcesErrorCode
	{
		ReplyIndexNotFound,
		ReplyIndexUnreadable,
		CodemodelReferenceMissing,
		CodemodelUnreadable,
		CodemodelParseError,
		CodemodelRootNotObject,
		CodemodelUnexpectedSchema,
		ConfigurationNotFound,
		AllMatchedTargetsUnreadable,
	};

	enum class GetSourcesWarningCode
	{
		NestedTargetReferenceArraysFlattened,
		MalformedTargetReference,
		TargetMissingJsonFile,
		TargetRootArrayNormalized,
		TargetKeyValueArrayNormalized,
		TargetReplyUnreadable,
	};

	struct GetSourcesWarning
	{
		GetSourcesWarningCode code;
		std::string targetName;
		FilePath path;
	};

	struct GetSourcesResult
	{
		std::vector<SourceEntry> entries;
		std::vector<GetSourcesWarning> warnings;
		std::size_t targetCount{0};
		std::size_t normalizedTargetCount{0};
		std::size_t matchedTargetCount{0};
		std::size_t malformedTargetReferenceCount{0};
		std::size_t emptyTargetReplyCount{0};
		std::size_t unreadableTargetReplyCount{0};
		std::size_t sourceObjectCount{0};
		std::size_t duplicateSourceCount{0};
	};

	using GetSourcesError = utility::ExpectedError<GetSourcesErrorCode>;
	using GetSourcesExpected = std::expected<GetSourcesResult, GetSourcesError>;

	explicit CMakeFileAPIReader(const FilePath& buildDir);

	// Returns the non-hidden configure preset names from CMakePresets.json and
	// CMakeUserPresets.json found in sourceDir. Returns an empty vector if no
	// presets file exists or none are visible.
	static std::vector<std::string> discoverPresets(const FilePath& sourceDir);

	// Resolves the binary directory for a named configure preset by running
	//   cmake -S <sourceDir> --preset <presetName> -N
	// and parsing the "Build directory:" line from its output.
	// Returns an empty FilePath on failure.
	static FilePath resolveBinaryDir(const FilePath& sourceDir, const std::string& presetName);

	// Returns true if a valid File API reply already exists in the build dir.
	bool hasReply() const;

	// Writes the query file. If the reply does not yet exist, triggers a
	// reconfigure. When sourceDir and presetName are provided, runs:
	//   cmake -S <sourceDir> --preset <presetName>
	// Otherwise falls back to:
	//   cmake <buildDir>
	// progress() is called with status messages during the cmake run.
	bool ensureReply(
		std::function<void(const std::string&)> progress = {},
		const FilePath& sourceDir = {},
		const std::string& presetName = {});

	// Returns all source entries across all targets in the given configuration.
	// Pass an empty string to use the first available configuration.
	// Returns an empty vector if the reply is absent or malformed.
	std::vector<SourceEntry> getSources(
		const std::string& configuration = {},
		const std::string& targetGlob = {}) const;

	// Detailed getSources API with typed errors and warning metadata.
	GetSourcesExpected getSourcesDetailed(
		const std::string& configuration = {},
		const std::string& targetGlob = {}) const;

	static std::string getSourcesErrorCodeToString(const GetSourcesErrorCode& code);
	static std::string getSourcesWarningCodeToString(const GetSourcesWarningCode& code);

	// Returns all CMake input files (CMakeLists.txt, .cmake files) listed in
	// the cmakeFiles-v1 reply. Watch these for staleness detection.
	std::vector<FilePath> getCMakeInputFiles() const;

	// Returns true if any CMake input file has been modified after the reply
	// index file was written, meaning the cached reply is stale and
	// ensureReply() should be called again before the next indexing run.
	// Returns false if the reply is absent (caller should call ensureReply()).
	bool isReplyStale() const;

	// Returns the build directory this reader was constructed with.
	const FilePath& buildDir() const;

private:
	FilePath m_buildDir;
	FilePath m_queryDir;
	FilePath m_replyDir;

	// Returns the path to the index-*.json reply file, or empty if absent.
	FilePath findIndexFile() const;

	// Returns the path to the toolchains-v1-*.json reply file, or empty if absent.
	FilePath findToolchainsFile() const;

	// Reads a JSON file from the reply directory and returns its contents.
	// Returns a null QJsonObject on failure.
	struct ReplyIndex;
	std::optional<ReplyIndex> readIndex() const;
};
