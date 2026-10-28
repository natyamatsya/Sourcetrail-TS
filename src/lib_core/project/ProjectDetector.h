#ifndef PROJECT_DETECTOR_H
#define PROJECT_DETECTOR_H

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "SourceGroupFields.h"
#include "SourceGroupType.h"

// What a folder looks like as a Sourcetrail project: the project the "New Project from
// Folder" wizard proposes, with the reasons for it. Paths are absolute, in generic form
// (forward slashes on Windows too).
//
// For C and C++, in this order:
//   1. CMakeLists.txt with CMakePresets.json / CMakeUserPresets.json: a CMake File API
//      group with a usable configure preset (visible, with a binaryDir, its generator
//      available, the environment it reads set), an already configured one first.
//   2. CMakeLists.txt otherwise: a CMake File API group without a preset, on a build
//      directory CMake configured for this source directory already (build*/,
//      cmake-build-*/, out/build/*/ ...), or one Sourcetrail lets CMake configure
//      (<project location>/<name>.srctrl-build) when indexing.
//   3. A compile_commands.json (the folder, build*/, cmake-build-*/, out/ ...): a
//      compilation database group.
//   4. Otherwise an empty C or C++ group on the folder: the source extensions found, a
//      language standard from the build files or the code, the include/ directories.
// Build directories (for an empty group) and vendored dependencies (third_party/,
// external/, vendor/ ... holding other projects, unless the build uses them) are
// proposed as excluded paths.
struct ProjectProposal
{
	std::string folder;
	std::string projectName;	// the folder's name, as a valid project name
	std::string projectLocation;	// where the project file goes
	SourceGroupType sourceGroupType = SourceGroupType::UNKNOWN;
	// The source group's fields by their SourceGroupFields names (source_paths,
	// cmake_preset, ...): what differs from a new group's defaults.
	std::map<std::string, SourceGroupField> fields;
	// Why: what was found and chosen, the first one a summary.
	std::vector<std::string> reasons;
	// What was found besides, deciding nothing: the git repository, its submodules,
	// build systems not detected yet.
	std::vector<std::string> notices;
};

struct ProjectDetectorOptions
{
	// Where the project file goes; empty: the folder.
	std::string projectLocation;
	// Whether a program is on PATH (cmake, ninja, make...); empty: looks there.
	std::function<bool(const std::string&)> hasExecutable;
};

enum class ProjectDetectionError : std::uint8_t
{
	FolderMissing,
	NotAFolder,
};

constexpr std::string_view to_std_sv(ProjectDetectionError error) noexcept
{
	using enum ProjectDetectionError;
	switch (error)
	{
	case FolderMissing:
		return "the folder does not exist";
	case NotAFolder:
		return "the path is not a folder";
	}
	return "unknown error";
}

[[nodiscard]] std::expected<ProjectProposal, ProjectDetectionError> detectProject(
	const std::string& folder, const ProjectDetectorOptions& options = {});

// The git repository a folder is in, read from its .git (a directory, or a file pointing
// to one, as in worktrees and submodules) without running git.
struct GitRepositoryInfo
{
	enum class Kind : std::uint8_t
	{
		Repository,
		Worktree,
		Submodule,
	};

	std::string root;	// the working tree's top directory
	std::string name;	// its directory's name
	Kind kind = Kind::Repository;
	std::string branch;	// empty when HEAD is detached
	std::string commit;	// HEAD's commit when detached (abbreviated)
};

[[nodiscard]] std::optional<GitRepositoryInfo> detectGitRepository(const std::string& folder);

// The submodules .gitmodules in `root` lists, with whether they are checked out (an
// empty directory: not initialised).
struct GitSubmodule
{
	std::string path;	// relative to root, as .gitmodules has it
	bool checkedOut = false;
};

[[nodiscard]] std::vector<GitSubmodule> readGitSubmodules(const std::string& root);

#endif	  // PROJECT_DETECTOR_H
