#ifndef BUILD_MODEL_SNAPSHOT_H
#define BUILD_MODEL_SNAPSHOT_H

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "FilePath.h"

enum class BuildModelProvider
{
	UNKNOWN,
	CMAKE_FILE_API,
	CARGO_METADATA
};

enum class BuildModelHealth
{
	COMPLETE,
	PARTIAL,
	FAILED
};

enum class BuildModelIssueSeverity
{
	WARNING,
	ERROR
};

enum class BuildModelIssueCode
{
	UNKNOWN,
	BUILD_REPLY_NOT_FOUND,
	BUILD_REPLY_UNREADABLE,
	BUILD_REPLY_MALFORMED,
	CONFIGURATION_NOT_FOUND,
	TARGET_REFERENCE_MALFORMED,
	TARGET_METADATA_MISSING,
	TARGET_METADATA_UNREADABLE,
	SOURCES_UNAVAILABLE,
	DATA_NORMALIZED
};

enum class BuildLanguage
{
	UNKNOWN,
	C,
	CXX,
	RUST
};

enum class BuildTargetKind
{
	UNKNOWN,
	EXECUTABLE,
	STATIC_LIBRARY,
	SHARED_LIBRARY,
	MODULE_LIBRARY,
	OBJECT_LIBRARY,
	INTERFACE_LIBRARY,
	UTILITY,
	CUSTOM
};

struct BuildModelIssue
{
	BuildModelIssueSeverity severity{BuildModelIssueSeverity::WARNING};
	BuildModelIssueCode code{BuildModelIssueCode::UNKNOWN};
	std::string targetName;
	FilePath path;
	std::string message;
};

struct BuildCompileGroupSnapshot
{
	BuildLanguage language{BuildLanguage::UNKNOWN};
	std::string compilerPath;
	FilePath sysroot;
	std::vector<FilePath> includes;
	std::vector<FilePath> systemIncludes;
	std::vector<FilePath> frameworkSearchPaths;
	std::vector<std::string> defines;
	std::vector<std::string> flags;
};

struct BuildFileSnapshot
{
	FilePath path;
	bool isGenerated{false};
	std::string targetName;
	std::string targetType;
	FilePath sourceDir;
	std::optional<BuildCompileGroupSnapshot> compileGroup;
};

struct BuildTargetSnapshot
{
	std::string name;
	BuildTargetKind kind{BuildTargetKind::UNKNOWN};
	FilePath sourceDir;
	std::size_t fileCount{0};
};

struct BuildModelSnapshot
{
	BuildModelProvider provider{BuildModelProvider::UNKNOWN};
	BuildModelHealth health{BuildModelHealth::COMPLETE};
	std::string configuration;
	std::string targetGlob;
	FilePath buildDir;

	std::vector<BuildFileSnapshot> files;
	std::vector<BuildTargetSnapshot> targets;
	std::vector<BuildModelIssue> issues;

	std::size_t targetCount{0};
	std::size_t normalizedTargetCount{0};
	std::size_t matchedTargetCount{0};
	std::size_t malformedTargetReferenceCount{0};
	std::size_t emptyTargetReplyCount{0};
	std::size_t unreadableTargetReplyCount{0};
	std::size_t sourceObjectCount{0};
	std::size_t duplicateSourceCount{0};
};

#endif	  // BUILD_MODEL_SNAPSHOT_H
