#include "SourceGroupRust.h"

#include "FileManager.h"
#include "IndexerCommandRust.h"
#include "RefreshInfo.h"
#include "SourceGroupSettingsRustEmpty.h"
#include "logging.h"
#include "utility.h"

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

bool SourceGroupRust::prepareIndexing()
{
	return true;
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
	// the crate root.
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

	// Emit one command per source group — the source file path is the working dir
	// itself (used only as a key for status tracking), not an individual .rs file.
	if (!info.filesToIndex.empty())
	{
		return {std::make_shared<IndexerCommandRust>(
			workingDir,
			indexedPaths,
			workingDir,
			m_settings->getCargoFeatures(),
			m_settings->getCargoAllFeatures(),
			m_settings->getCargoNoDefaultFeatures(),
			m_settings->getCargoTargetTriple(),
			m_settings->getRustSpecializationScope())};
	}
	return {};
}

std::shared_ptr<SourceGroupSettings> SourceGroupRust::getSourceGroupSettings()
{
	return m_settings;
}

std::shared_ptr<const SourceGroupSettings> SourceGroupRust::getSourceGroupSettings() const
{
	return m_settings;
}
