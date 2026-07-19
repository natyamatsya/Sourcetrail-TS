#include "SourceGroupRust.h"

#include <nlohmann/json.hpp>

#include "FileManager.h"
#include "IndexerCommandRust.h"
#include "RefreshInfo.h"
#include "SourceGroupSettingsRustEmpty.h"
#include "logging.h"
#include "utility.h"
#include "utilityApp.h"

namespace
{
//! Crate fan-out R1b: enumerate the workspace member crate roots with cargo
//! itself — exact member/glob/exclude resolution, no manifest parsing on our
//! side. Empty result = enumeration failed; the caller falls back to one
//! whole-workspace command (legacy behavior).
std::vector<FilePath> enumerateWorkspaceMemberRoots(const FilePath& workspaceDir)
{
	const utility::ProcessOutput output = utility::executeProcess(
		"cargo",
		{"metadata", "--no-deps", "--format-version", "1"},
		workspaceDir,
		false,
		std::chrono::milliseconds(60000));
	if (output.exitCode != 0)
	{
		LOG_WARNING(
			"cargo metadata failed (exit " + std::to_string(output.exitCode) + ") in \"" +
			workspaceDir.str() + "\" — indexing the workspace as one command. " + output.error);
		return {};
	}

	std::vector<FilePath> memberRoots;
	try
	{
		const nlohmann::json metadata = nlohmann::json::parse(output.output);
		for (const auto& package: metadata.at("packages"))
		{
			memberRoots.push_back(
				FilePath(package.at("manifest_path").get<std::string>()).getParentDirectory());
		}
	}
	catch (const std::exception& e)
	{
		LOG_WARNING(std::string("cargo metadata parse failed: ") + e.what());
		return {};
	}
	return memberRoots;
}
}  // namespace

namespace
{
// Nearest directory at or above `path` that contains a Cargo.toml — the same
// upward walk rust-analyzer's project discovery performs from the working
// directory.
FilePath findCargoRootAtOrAbove(FilePath path)
{
	if (!path.isDirectory())
		path = path.getParentDirectory();
	while (!path.empty())
	{
		if (path.getConcatenated("/Cargo.toml").exists())
			return path;
		const FilePath parent = path.getParentDirectory();
		if (parent == path)
			break;
		path = parent;
	}
	return FilePath();
}
}	 // namespace

SourceGroupRust::SourceGroupRust(const std::shared_ptr<SourceGroupSettingsRustEmpty>& settings)
	: m_settings{settings}
{
}

std::expected<void, PrepareIndexingError> SourceGroupRust::prepareIndexing()
{
	return {};
}

std::set<FilePath> SourceGroupRust::filterToContainedFilePaths(
	const std::set<FilePath>& filePaths) const
{
	return filterToContainedSourceFilePath(filePaths);
}

std::set<FilePath> SourceGroupRust::getAllSourceFilePaths() const
{
	FileManager fileManager;
	fileManager.update(
		m_settings->getSourcePathsExpandedAndAbsolute(),
		m_settings->getExcludeFiltersExpandedAndAbsolute(),
		m_settings->getSourceExtensions());
	return fileManager.getAllSourceFilePaths();
}

std::vector<std::shared_ptr<IndexerCommand>> SourceGroupRust::getIndexerCommands(
	const RefreshInfo& info) const
{
	const std::set<FilePath> indexedPaths =
		utility::toSet(m_settings->getSourcePathsExpandedAndAbsolute());

	// The Rust indexer works at crate level via working_directory (Cargo.toml).
	// An explicitly configured workspace directory wins (mirroring the CMake
	// File API group's source_directory); otherwise prefer the project file's
	// directory when it is part of a cargo project, then fall back to the
	// first source path that is, so the .srctrl.toml does not have to live in
	// the crate root. This resolved directory is the workspace root that crate
	// fan-out (R1b) enumerates members from.
	FilePath workingDir = m_settings->getCargoWorkspaceDirectoryExpandedAndAbsolute();
	if (workingDir.empty())
		workingDir = findCargoRootAtOrAbove(m_settings->getProjectDirectoryPath());
	if (workingDir.empty())
	{
		for (const FilePath& sourcePath : m_settings->getSourcePathsExpandedAndAbsolute())
		{
			workingDir = findCargoRootAtOrAbove(sourcePath);
			if (!workingDir.empty())
				break;
		}
	}
	if (workingDir.empty())
	{
		LOG_WARNING(
			"Rust source group: no Cargo.toml found at or above the project directory or any "
			"source path; the indexer will likely fail to load a cargo project.");
		workingDir = m_settings->getProjectDirectoryPath();
	}

	if (info.filesToIndex.empty())
	{
		return {};
	}

	auto makeCommand = [&](const FilePath& crateRoot, bool restrictToPackage) {
		// The source file path doubles as the status-tracking key.
		return std::make_shared<IndexerCommandRust>(
			crateRoot,
			indexedPaths,
			crateRoot,
			m_settings->getCargoFeatures(),
			m_settings->getCargoAllFeatures(),
			m_settings->getCargoNoDefaultFeatures(),
			m_settings->getCargoTargetTriple(),
			m_settings->getRustSpecializationScope(),
			restrictToPackage);
	};

	// Crate fan-out R1b: one command per workspace member crate, so K Rust
	// supervisors (R1) can index crates concurrently. Member commands carry
	// restrict_to_package, telling the subprocess to collect only the
	// commanded package (exact Cargo.toml-dir match). When enumeration fails,
	// fall back to the legacy whole-workspace command with the flag unset, so
	// the subprocess collects every workspace crate — even when the workspace
	// root is itself a package.
	std::vector<std::shared_ptr<IndexerCommand>> commands;
	for (const FilePath& memberRoot: enumerateWorkspaceMemberRoots(workingDir))
	{
		commands.push_back(makeCommand(memberRoot, true));
	}
	if (commands.empty())
	{
		commands.push_back(makeCommand(workingDir, false));
	}
	return commands;
}

std::shared_ptr<SourceGroupSettings> SourceGroupRust::getSourceGroupSettings()
{
	return m_settings;
}

std::shared_ptr<const SourceGroupSettings> SourceGroupRust::getSourceGroupSettings() const
{
	return m_settings;
}
