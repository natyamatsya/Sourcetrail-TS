#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
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
		std::vector<FilePath> precompiledHeaders; // target_precompile_headers inputs
	};

	struct SourceEntry
	{
		FilePath path;
		bool isGenerated{false};
		std::optional<CompileGroup> compileGroup;
		std::string targetName;
		std::string targetType;	// EXECUTABLE, STATIC_LIBRARY, SHARED_LIBRARY, ...
		FilePath sourceDir;		// CMake source directory (paths.source from codemodel)
		// The TARGET's own directories (absolute; from the target reply's `paths`, which are
		// relative to the top-level dirs). A target added in a subdirectory keeps its object
		// files -- and the .modmap module response files next to them -- under
		// <targetBuildDir>/CMakeFiles/<name>.dir/, NOT under the top-level build dir.
		FilePath targetBuildDir;
		FilePath targetSourceDir;
	};

	struct TargetEntry
	{
		std::string name;
		std::string type;	// EXECUTABLE, STATIC_LIBRARY, SHARED_LIBRARY, ...
		FilePath sourceDir;
		std::vector<std::string> dependencies;
	};

	struct JsonEntryPoint
	{
		std::string kind;
		FilePath path;
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
		std::vector<TargetEntry> targets;
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

	// A visible configure preset (not hidden, condition true on this host), as
	// `cmake --list-presets` shows it.
	struct PresetInfo
	{
		std::string name;
		std::string displayName;	// may be empty
		std::string description;	// may be empty
		std::string generator;	// inherited; empty when the preset leaves it to CMake
		// The environment variables the preset's binaryDir, toolchainFile and cacheVariables
		// read ($env{} / $penv{}) that neither its environment nor this process sets: a
		// configure with it is likely to fail (a vcpkg toolchain without VCPKG_ROOT...).
		std::vector<std::string> undefinedEnvironment;
	};

	enum class ConfigureErrorCode : std::uint8_t
	{
		QueryNotWritable,
		CMakeNotStarted,
		CMakeTimedOut,
		CMakeFailed,
		NoReplyWritten,
	};

	// The message is CMake's own output (its errors) where it ran.
	using ConfigureError = utility::ExpectedError<ConfigureErrorCode>;

	explicit CMakeFileAPIReader(const FilePath& buildDir);

	// The visible configure presets of CMakePresets.json and CMakeUserPresets.json
	// (and the files they include) in sourceDir, in file order: hidden presets and
	// presets whose `condition` is false on this host are left out, as
	// `cmake --list-presets` does. Empty if there is no presets file.
	[[nodiscard]] static std::vector<PresetInfo> discoverPresetInfos(const FilePath& sourceDir);

	// The names of discoverPresetInfos().
	static std::vector<std::string> discoverPresets(const FilePath& sourceDir);

	// The preset files read for sourceDir: CMakePresets.json, CMakeUserPresets.json
	// and everything they include (those that exist).
	static std::vector<FilePath> getPresetFiles(const FilePath& sourceDir);

	// Resolves the binary directory of a configure preset from the presets files:
	// its (inherited) binaryDir with the preset macros expanded, relative to
	// sourceDir. Returns an empty FilePath when the preset is unknown or has no
	// binaryDir.
	static FilePath resolveBinaryDir(const FilePath& sourceDir, const std::string& presetName);

	// Returns true if a valid File API reply already exists in the build dir.
	bool hasReply() const;

	// Returns the JSON entry points listed in index-*.json (kind + resolved file path).
	std::vector<JsonEntryPoint> getJsonEntryPoints() const;

	// Writes the query file and, if there is no reply yet or `reconfigure` is set,
	// runs CMake to (re)generate it. When sourceDir and presetName are provided, runs:
	//   cmake -S <sourceDir> --preset <presetName>
	// with sourceDir alone:
	//   cmake -S <sourceDir> -B <buildDir>
	// Otherwise falls back to:
	//   cmake <buildDir>
	// progress() is called with status messages during the cmake run.
	[[nodiscard]] std::expected<void, ConfigureError> ensureReply(
		std::function<void(const std::string&)> progress = {},
		const FilePath& sourceDir = {},
		const std::string& presetName = {},
		bool reconfigure = false);

	// The configuration names the reply has (one for single-config generators,
	// CMAKE_CONFIGURATION_TYPES for multi-config ones). Empty without a reply.
	std::vector<std::string> getConfigurations() const;

	// A compileCommandFragment's arguments, unquoted as the generator's shell would: on
	// Windows as a command line, elsewhere as a POSIX shell does, single quotes included
	// (the Xcode generator writes '-std=c++17').
	[[nodiscard]] static std::vector<std::string> splitCompileFragment(const std::string& fragment);

	// Whether a target name matches a target filter: empty matches every target,
	// otherwise one or more wildcard patterns separated by ';' or ','.
	[[nodiscard]] static bool matchesTargetFilter(const std::string& targetName, const std::string& targetFilter);

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

	// Returns true if any CMake input file -- or, given the source directory, a
	// presets file -- has been modified after the reply index file was written,
	// meaning the cached reply is stale and ensureReply(..., reconfigure = true)
	// should run before the next indexing run.
	// Returns false if the reply is absent (caller should call ensureReply()).
	bool isReplyStale(const FilePath& sourceDir = {}) const;

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

constexpr std::string_view to_std_sv(CMakeFileAPIReader::ConfigureErrorCode code) noexcept
{
	using enum CMakeFileAPIReader::ConfigureErrorCode;
	switch (code)
	{
	case QueryNotWritable:
		return "the CMake File API query file could not be written";
	case CMakeNotStarted:
		return "cmake could not be started (is it on PATH?)";
	case CMakeTimedOut:
		return "cmake did not finish in time";
	case CMakeFailed:
		return "CMake failed to configure the project";
	case NoReplyWritten:
		return "CMake configured the project but wrote no File API reply";
	}
	return "unknown error";
}
